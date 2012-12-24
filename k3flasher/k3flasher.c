/*
* Kindle 3 fastboot tool in the making
*
* uses libusbx or libusb-1.0
* needs RAM Kernel from Freescale ATK
*
* this software is published under the know-what-you're-doing licence:
*
* 1. you may use this software for whatever you want, given that you know what you're doing.
* 2. author of this software isn't responsible for anything since you knew what you're doing.
* 3. if you have still questions, you do not fullfill requirement #1
*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef NBD_SERVER
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <libusb.h>

/** USB device matching constants */
#define IMX35_USBLOADER_VID	0x15A2
#define IMX35_USBLOADER_PID	0x0030
#define IMX35_USBLOADER_IFACE	0
#define IMX35_USBLOADER_EP_IN	0x82
#define IMX35_USBLOADER_EP_OUT	0x01

/** ROM kernel will typically return this to flag success/ready */
#define ROM_KERNEL_STATUS_OK	0x56787856
/** ROM kernel will return this upon successful writes to memory */
#define ROM_KERNEL_MEMSET_OK	0x128A8A12

/** maximum transfer unit for a single bulk transfer for sending data to the device */
#define MAX_TRANSFER		16*1024
/** maximum number of bytes to transfer for a single flash program or dump run */
#define MAX_CHUNK		0x00200000

/** where to write the RAM kernel in device's RAM layout */
#define RAM_KERNEL_ADDRESS	0x80004000

/** size of the flash memory (only checked on my own device) */
#define DEVICE_STORAGE_SIZE	0xee800000

/** a global context pointer for libusb */
libusb_context *lusb_ctx = NULL;

/** a global (for the sake of not passing it around too much) device handle for libusb */
libusb_device_handle *lusb_dev = NULL;

/**
* A convenience method for sending data to device
*
* \param sendbuf a pointer to a block of memory that should be send
* \param length size of that block of memory
* \param sent if not NULL, the int value pointed to is set to the number of bytes actually transferred
* \return 0 if successful, -1 if we couldn't send the requested number of bytes, otherwise error from libusb
*/
int usb_send(uint8_t *sendbuf, int length, int *sent) {
	int transferred;
	int err = libusb_bulk_transfer(lusb_dev, IMX35_USBLOADER_EP_OUT, sendbuf, length, &transferred, 0);
	if(err) {
		fprintf(stderr, "E: sending data to device: %s\n", libusb_error_name(err));
		return err;
	}
	if(sent != NULL) {
		*sent = transferred;
		return 0;
	}
	if(transferred != length) {
		fprintf(stderr, "E: wrong transfer length, wanted to send %d bytes but sent %d bytes.\n",
			length, transferred);
		return -1;
	}
	return 0;
}

/**
* A convenience method for receiving data from device
*
* \param recbuf pointer to where the data should be stored
* \param length number of bytes to receive
* \param received if not NULL, the int value pointed to is set to the number of bytes actually received
* \return 0 if successful, -1 if the number of received bytes does not match expectations, otherwise error from libusb
*/
int usb_receive(uint8_t *recbuf, int length, int *received) {
	int transferred;
	int err = libusb_bulk_transfer(lusb_dev, IMX35_USBLOADER_EP_IN, recbuf, length, &transferred, 0);
	if(err) {
		fprintf(stderr, "E: reading data from device: %s\n", libusb_error_name(err));
		return err;
	}
	if(received != NULL) {
		*received = transferred;
		return 0;
	}
	if(transferred != length) {
		fprintf(stderr, "E: wrong transfer length, wanted to receive %d bytes but received %d bytes.\n",
			length, transferred);
		return -1;
	}
	return 0;
}

/**
* A convenience method for sending "commands" to the device
*
* "Commands" are of a fixed length of 16 bytes for both the ROM kernel
* and the RAM kernel. This function allows to assemble them similar to
* printf(). It takes a format string, which may contain the following
* format specifiers:
* "1", "2", "4": will put 1, 2 or 4 bytes and set them to the value
* of an int value from the variable argument list in protocol byte order
* "." will put a single \0 byte.
*
* \param fmt is a pointer to a format spec string
* \param ... is a list of values to be used to assemble the command
* \return 0 if successful
*/
int send_command(char *fmt, ...) {
	va_list argp;
	uint8_t sendbuf[16];
	int ptr = 0;
	memset(sendbuf, 0, 16);

	va_start(argp, fmt);
	while(*fmt != '\0') {
		if(*fmt == '1') {
			sendbuf[ptr++] = (uint8_t) va_arg(argp, int);
		} else if(*fmt == '2') {
			uint16_t v = va_arg(argp, int);
			sendbuf[ptr++] = (v & 0xFF00) >> 8;
			sendbuf[ptr++] = v & 0xFF;
		} else if(*fmt == '4') {
			uint32_t v = va_arg(argp, int);
			sendbuf[ptr++] = (v & 0xFF000000) >> 24;
			sendbuf[ptr++] = (v & 0xFF0000) >> 16;
			sendbuf[ptr++] = (v & 0xFF00) >> 8;
			sendbuf[ptr++] = v & 0xFF;
		} else if(*fmt == '.') {
			ptr += 1;
		}
		fmt++;
	}
	va_end(argp);

	return usb_send(sendbuf, 16, NULL);
}

/**
* A convenience method for reading replys from the device
*
* Replies might have variable length, so the first argument is the
* number of bytes we expect.
* Otherwise, it resembles scanf() a bit. It will take a format string
* and optional list of pointers where data will be written to.
* The format string might contain the following format specifiers:
* "1", "2", "4": read numerical value expressed in 1, 2 or 4 bytes
* and take protocol byte order into account. Read value will be written
* to the value pointed to by a given uint{32,16,8}_t* in the varargs list.
* "." skips a byte
* "p": read a pointer to a uint8_t* pointer from varargs list and set
* the pointer pointed to by it to the read buffer - and return. Note
* that the pointer to the read buffer will have to be freed by the
* caller in this special case.
*
* \param fmt is a pointer to a format spec string
* \param ... is a list of pointers to be used to write the data to
* \return 0 if successful
*/
int read_reply(size_t length, char *fmt, ...) {
	va_list argp;
	uint8_t *recbuf;
	int ptr = 0;

	recbuf = calloc(1, length);
	if(recbuf == NULL) {
		fprintf(stderr, "E: could not allocate receive buffer memory.\n");
		return -1;
	}

	if(usb_receive(recbuf, length, NULL)) {
		free(recbuf);
		return -1;
	}

	va_start(argp, fmt);
	while(*fmt != '\0') {
		if(*fmt == 'p') {
			*((uint8_t**) va_arg(argp, uint8_t**)) = recbuf;
			va_end(argp);
			return 0;
		} else if(*fmt == '1') {
			*((uint8_t*) va_arg(argp, uint8_t*)) = recbuf[ptr++];
		} else if(*fmt == '2') {
			*((uint16_t*) va_arg(argp, uint16_t*)) = recbuf[ptr] << 8 | recbuf[ptr+1];
			ptr += 2;
		} else if(*fmt == '4') {
			*((uint32_t*) va_arg(argp, uint32_t*)) =
				recbuf[ptr] << 24 |
				recbuf[ptr+1] << 16 |
				recbuf[ptr+2] << 8 |
				recbuf[ptr+3];
			ptr += 4;
		} else if(*fmt == '.') {
			ptr += 1;
		}
		fmt++;
	}
	va_end(argp);

	free(recbuf);
	return 0;
}


/**
* ROM kernel interface: write data to memory
*
* it can at least write 8bit and 32bit values, others are not 
* tested or even implemented.
*
* \param length target value size, either 32 or 8
* \param address memory address to write value to
* \param value what value to write
* \return 0 on success
*/
int mem_write(int length, uint32_t address, uint32_t value) {
	uint32_t status;
	if(length == 32) {
		// either 32bit...
		if(send_command("241....4", 0x0202, address, length, value)) return -1;
	} else {
		// ...or 8 bit. no idea if there's anything else.
		if(send_command("241.......1", 0x0202, address, 8, value)) return -1;
	}
	if(read_reply(4, "4", &status)) return -1;
	if(status != ROM_KERNEL_STATUS_OK) {
		fprintf(stderr, "E: kindle reported an error: 0x%08x\n", status);
		return -1;
	}
	if(read_reply(4, "4", &status)) return -1;
	if(status != ROM_KERNEL_MEMSET_OK) {
		fprintf(stderr, "E: kindle reported a memset error: 0x%08x\n", status);
		return -1;
	}
	return 0;
}

/**
* ROM kernel: wrapper for initializing SoC registers
*
* \return 0 on success
*/
int init_registers() {
	// send 32bit memory write - whatever this might be for...
	if(mem_write(32, 0xFFFFFFFF, 0)) return -1;

	// memory initialization for iMX35 using MDDR
	// setting SoC registers (0xB8...),
	// no idea what the RAM accesses in between are for (timing?)
	if(mem_write(32, 0xB8001010, 0x0000004C)) return -1;
	if(mem_write(32, 0xB8001004, 0x006ac73a)) return -1;
	if(mem_write(32, 0xB8001000, 0x92100000)) return -1;
	if(mem_write(32, 0x80000f00, 0x12344321)) return -1;
	if(mem_write(32, 0xB8001000, 0xa2100000)) return -1;
	if(mem_write(32, 0x80000000, 0x12344321)) return -1;
	if(mem_write(32, 0x80000000, 0x12344321)) return -1;
	if(mem_write(32, 0xB8001000, 0xb2100000)) return -1;
	if(mem_write(8, 0x80000033, 0xda)) return -1;
	if(mem_write(8, 0x81000000, 0xff)) return -1;
	if(mem_write(32, 0xB8001000, 0x82226080)) return -1;
	if(mem_write(32, 0xB8001000, 0x82226007)) return -1;
	if(mem_write(32, 0x80000000, 0xDEADBEEF)) return -1;
	if(mem_write(32, 0xB8001010, 0x0000000c)) return -1;
	if(mem_write(32, 0x80000000, 0x00000001)) return -1;

	return 0;
}

/**
* ROM kernel: read reply and compare it to an expected status code (32bit)
*
* \return 0 if returned status matches the value to check
*/
int check_romkernel_status(uint32_t check) {
	uint32_t status;
	if(read_reply(4, "4", &status)) return -1;
	if(status != check) {
		fprintf(stderr, "E: got no usable answer to status query. giving up.\n");
		return -1;
	}
	return 0;
}

/**
* ROM kernel: upload a RAM kernel image
*
* \param filename name of a file containing the RAM kernel. Must be seek()able.
* \return 0 on success
*/
int upload_ramkernel(char* filename) {
	FILE *kernel;
	struct stat kernel_stat;
	int fd, err, i;
	size_t reads, buf_size;
	uint8_t xmit_buf[MAX_TRANSFER];

	// try to open kernel image
	kernel = fopen(filename, "rb");
	if(NULL == kernel) {
		fprintf(stderr, "E: opening RAM kernel image: %s\n", strerror(errno));
		return -1;
	}

	fd = fileno(kernel);
	if(fd == -1) {
		fprintf(stderr, "E: aquiring fileno(): %s\n", strerror(errno));
		goto fail;
	}

	err = fstat(fd, &kernel_stat);
	if(err) {
		fprintf(stderr, "E: stat()ing kernel image error: %s\n", strerror(errno));
		goto fail;
	}

	if(kernel_stat.st_size < 1024) {
		fprintf(stderr, "E: image size needs to be at least 1024 bytes\n");
		goto fail;
	}

	/* upload large chunk of data */
	if(send_command("24.4....1", 0x0404, RAM_KERNEL_ADDRESS, kernel_stat.st_size, 0)) goto fail;
	if(check_romkernel_status(ROM_KERNEL_STATUS_OK)) goto fail;
	for(i = 0; i < kernel_stat.st_size; ) {
		buf_size = MAX_TRANSFER;
		if(i+buf_size > kernel_stat.st_size) buf_size = 32; // fall back to 32 byte transmits
		if(i+buf_size > kernel_stat.st_size) buf_size = kernel_stat.st_size - i; // remainder

		reads = fread(xmit_buf, 1, buf_size, kernel);
		if(reads != buf_size) {
			fprintf(stderr, "E: reading RAM kernel\n");
			goto fail;
		}

		if(usb_send(xmit_buf, buf_size, NULL)) goto fail;
		i += buf_size;
	}

	/*
	 * we must send the first 1kByte again?!?
	 * this time, the command has a different last byte, 0xAA instead of 0.
	 * is this to bring the SoC's cache lines up to matters?
	 */
	if(fseek(kernel, 0, SEEK_SET)) {
		fprintf(stderr, "E: cannot seek back in RAM kernel image: %s\n", strerror(errno));
		goto fail;
	}
	if(send_command("24.4....1", 0x0404, RAM_KERNEL_ADDRESS, 1024, 0xAA)) goto fail;
	if(check_romkernel_status(ROM_KERNEL_STATUS_OK)) goto fail;
	for(i = 0; i < 1024; i+=64) {
		reads = fread(xmit_buf, 1, 64, kernel);
		if(reads != 64) {
			fprintf(stderr, "E: reading RAM kernel (2nd run)\n");
			goto fail;
		}

		if(usb_send(xmit_buf, 64, NULL)) goto fail;
	}
	fclose(kernel);
	return 0;

fail:
	fclose(kernel);
	return -1;
}

/**
* RAM kernel: wrapper for initializing flash memory
*
* This will also set the flags for transmissions, e.g. dumping or
* programming flash memory.
*
* \return 0 on success
*/
int flash_init() {
	uint32_t flag;
	uint16_t ack;

	/* init flash */
	if(send_command("22", 0x0606, 0x0001)) return -1;
	if(read_reply(8, "2......", &ack)) return -1;
	if(ack != 0) {
		fprintf(stderr, "E: got error status 0x%04x\n", ack);
		return -1;
	}

	/* set transmission flags to 0 */
	for(flag = 0x0302; flag <= 0x0305; flag++) {
		if(send_command("22....4", 0x0606, flag, 0)) return -1;
		if(read_reply(8, "2", &ack)) return -1;
		if(ack != 0) {
			fprintf(stderr, "E: got error status 0x%04x\n", ack);
			return -1;
		}
	}

	return 0;
}

/**
* RAM kernel: wrapper for dumping flash memory
*
* Note that address and length should be multiples of 512.
*
* \param address the address within flash memory to start reading data at
* \param length the length of the data to be read in bytes (<= MAX_CHUNK)
* \param data pointer to an uint8_t* pointer, which will be set to a memory block containing the data read. must be freed by caller.
* \return 0 if successful
*/
int flash_dump_data(uint32_t address, uint32_t length, uint8_t** data) {
	uint32_t reads;
	uint8_t* recbuf;
	uint32_t size;
	uint16_t ack;

	*data = malloc(length);
	if(NULL == data) {
		fprintf(stderr, "E: claiming buffer memory\n");
		return -1;
	}

	fprintf(stderr, "I: downloading 0x%08x (=%d) bytes, starting at 0x%08x\n", length, length, address);
	if(send_command("2244...1", 0x0606, 0x0003, address, length, 0)) return -1;
	for(reads = 0; reads < length; ) {
		if(read_reply(8, "2..4", &ack, &size)) return -1;
		if(ack == 1) {
			/* slightly awkward, maybe refactor read_reply? or use usb_receive? */
			if(read_reply(size, "p", &recbuf)) return -1;
			memcpy(&(*data)[reads], recbuf, size);
			free(recbuf);
			reads += size;
			if(!(reads % 0x10000)) /* 64k steps */
				fprintf(stderr, "I: read 0x%08x (=%u) bytes\n", reads, reads);
		} else {
			fprintf(stderr, "E: unexpected ack value 0x%04x\n", ack);
			return -1;
		}
	}

	return 0;
}

/**
* RAM kernel: wrapper for dumping flash memory to a file on host side
*
* Note that address and length should be multiples of 512.
*
* \param address the address within flash memory to start reading data at
* \param length the length of the data to be read in bytes
* \param filename the name of the output file
* \return 0 if successful
*/
int flash_dump(uint32_t address, uint32_t length, char *filename) {
	FILE *dump;
	uint8_t* recbuf;
	int set_size = MAX_CHUNK;
	unsigned int done = 0;
	size_t write_size __attribute__((unused));
	struct stat sb;

	if (stat(filename, &sb) == -1) {
		dump = fopen(filename, "wb");
	} else {
		done = sb.st_size;
		dump = fopen(filename, "ab");
	}

	fprintf(stderr, "I: dump file %s offset 0x%08x\n", filename, done);

	if(NULL == dump) {
		fprintf(stderr, "E: opening dump file: %s\n", strerror(errno));
		return -1;
	}

	while(done < length) {
		if(set_size + done > length) {
			set_size = length - done;
		}
		if(flash_dump_data(address + done, set_size, &recbuf)) {
			fprintf(stderr, "E: dumping data\n");
			fclose(dump);
			return -1;
		}
		write_size = fwrite(recbuf, set_size, 1, dump);
		free(recbuf);
		done += set_size;
	}

	fclose(dump);
	return 0;
}

/**
* RAM kernel: wrapper for flashing data
*
* Note that the address must be a multiple of 512. Lengths that are not
* multiples of 512 seem to work, however.
*
* \param address the address within the flash memory to start reading at
* \param data is a block of data to be flashed (lower or equal than MAX_CHUNK)
* \param length length of the data to flash
* \param cont a continuation flag, should be set to 1 if another call is forthcoming, 0 otherwise
* \return 0 if successful
*/
int flash_program_data(uint32_t address, uint8_t *data, uint32_t length, uint8_t cont) {
	int stage;
	int done;
	uint16_t ack;
	uint32_t size;

	fprintf(stderr, "I: writing 0x%08x (=%u) bytes to address 0x%08x, waiting for completion...\n", length, length, address);

	/* send flash command for the current run */
	if(send_command("2244211", 0x0606, 0x0005,
		address, length, 1 /* readback check */, (cont?0:1), 0 /* format */))
		goto fail;

	/* read and check reply */
	if(read_reply(8, "2..4", &ack, &size)) goto fail;
	if(ack != 0 || size != length) {
		fprintf(stderr, "E: unexpected ack/size returned: 0x%04x/0x%08x\n", ack, size);
		goto fail;
	}

	/* send data for the current run */
	for(done = 0; done < length; ) {
		/* we send data in packets of certain lengths (MAX_TRANSFER, 64, 32 or lower) */
		int buf_size = MAX_TRANSFER;
		if(done + buf_size > length) buf_size = 64; /* fall back to 64 byte transmits */
		if(done + buf_size > length) buf_size = 32; /* fall back to 32 byte transmits */
		if(done + buf_size > length) buf_size = length - done; /* remainder */

		if(usb_send(&data[done], buf_size, NULL)) goto fail;
		done += buf_size;
	}

	/*
	 * now we collect flash status reports from the device, which happens in three stages.
	 * the first stage will ack 512byte block writes being done, the second stage will
	 * report a checksum of some kind and the third stage will report an overall success
	 * code.
	 */

	fprintf(stderr, "I: wrote 0x%08x (=%d) bytes, waiting for completion...\n", done, done);
	stage = 0;
	done = 0;
	while(stage < 2) {
		/* all reports are 8 bytes long */
		if(read_reply(8, "2..4", &ack, &size)) goto fail;
		/* different analysis depending on state: */
		if(stage == 0 && ack == 1) {
			/* byte count, better do not print every 512byte */
			done += size;
			if(!(done % 0x10000)) fprintf(stderr, "I: flashed 0x%08x (=%d) bytes\n", done, done); /* 64k steps */
		} else if(stage == 0 && ack == 3) {
			/* again a byte count over the full range? */
			fprintf(stderr, "I: flashing of 0x%08x (=%u) bytes complete\n", size, size);
			stage = 1;
		} else if(stage == 1 && ack == 0) {
			stage = 2;
		} else {
			fprintf(stderr, "E: unexpected status: 0x%04x\n", ack);
			goto fail;
		}
	}

	return 0;
fail:
	return -1;
}

/**
* RAM kernel: wrapper for writing data from a file on host to device flash
*
* Note that the address must be a multiple of 512. Lengths that are not
* multiples of 512 seem to work, however. Length is determined from file
* size.
*
* \param address the address within the flash memory to start reading at
* \param filename the name of the file on host containing the data to write
* \return 0 if successful
*/
int flash_program(uint32_t address, char *filename) {
	int fd, err;
	size_t reads;
	FILE *image;
	struct stat image_stat;
	uint8_t *xmit_buf;
	int cont;	/* flag that signals more data chunks to come */
	int set_size = MAX_CHUNK; /* data to send for a single flash operation */
	int done = 0;	/* counter for data that is processed (in bytes) */

	xmit_buf = malloc(set_size);
	if(NULL == xmit_buf) {
		fprintf(stderr, "E: unable to claim memory for buffer\n");
		return -1;
	}

	image = fopen(filename, "rb");
	if(NULL == image) {
		fprintf(stderr, "E: opening image for flashing: %s\n", strerror(errno));
		return -1;
	}

	fd = fileno(image);
	if(fd == -1) {
		fprintf(stderr, "E: aquiring fileno() for image: %s\n", strerror(errno));
		goto fail;
	}

	err = fstat(fd, &image_stat);
	if(err) {
		fprintf(stderr, "E: stat()ing flash image error: %s\n", strerror(errno));
		goto fail;
	}

	if(image_stat.st_size % 512) {
		fprintf(stderr, "W: size is not a multiple of 512, remainder will be padded/overwritten\n");
	}

	/* actual flashing: */

	fprintf(stderr, "I: flashing 0x%08jx (=%jd) bytes\n", (uintmax_t) image_stat.st_size, (intmax_t) image_stat.st_size);
	while(done < image_stat.st_size) {
		/* check and set continuation flag */
		if(done + set_size > image_stat.st_size) {
			/* we can send the remainder in a (last) single run */
			set_size = image_stat.st_size - done;
			cont = 0;
		} else {
			/* we still have more to send after this run */
			cont = 1;
		}

		reads = fread(xmit_buf, 1, set_size, image);
		if(reads < set_size) {
			fprintf(stderr, "E: reading image: %s\n", strerror(errno));
			goto fail;
		}

		if(flash_program_data(address + done, xmit_buf, reads, cont)) {
			fprintf(stderr, "E: flashing data\n");
			goto fail;
		}

		done += reads;
	};

	fclose(image);
	return 0;
fail:
	fclose(image);
	return -1;
}

/**
* RAM kernel: get flash information
*
* will output to stderr
*
* \return 0 on successful operation
*/
int flash_info() {
	uint16_t ack;
	uint32_t size;
	uint8_t *devinfo;

	/* send flash info command */
	if(send_command("22", 0x0606, 0x0006)) return -1;
	/* parse reply */
	if(read_reply(8, "2..4", &ack, &size)) return -1;
	if(ack != 0) {
		fprintf(stderr, "E: device returned error 0x%04x\n", ack);
		return -1;
	}
	/* it never reports the correct size here for me */
	fprintf(stderr, "I: got size %d (probably wrong)\n", size);

	/* read special data section */
	if(flash_dump_data(0x00040c00, 0x200, &devinfo)) {
		fprintf(stderr, "E: dumping data\n");
		return -1;
	}

	fprintf(stderr, "I: DO NOT MAKE THE FOLLOWING INFORMATION PUBLIC!\n");
	fprintf(stderr, "I: if you need to discuss it in public, obfuscate\n");
	fprintf(stderr, "I: e.g. the second half of the numbers.\n");
	fprintf(stderr, "I: got device serial number <");
	fwrite(&devinfo[0], 16, 1, stderr);
	fprintf(stderr, ">\n");

	fprintf(stderr, "I: got device Wifi MAC <");
	fwrite(&devinfo[88], 12, 1, stderr);
	fprintf(stderr, ">\n");

	free(devinfo);

	return 0;
}

#ifdef NBD_SERVER
/**
* wrapper that implements a single-threaded NBD daemon providing the flash contents
*
* will provide only one possible connection and will stop afterwards.
* only IPv4/TCP transport for now.
*
* \param port TCP port number to listen on
* \return 0 on success
*/
int flash_nbd(int port) {
	int sockfd, conn, result, ret, c;
	size_t reads;
	struct sockaddr_in addr;
	fd_set fds;
	uint8_t *buf;

	/* we define this ourselves here in order to keep simple. probably not clever. */
	struct {
		char INIT_PASSWD[8];
		char cliserv_magic[8];
		uint32_t export_size_filler;
		uint32_t export_size;
		uint32_t flags;
		char reserved[124];
	} __attribute__((packed)) initmsg = {
		{'N', 'B', 'D', 'M', 'A', 'G', 'I', 'C'},
		{0, 0, 0x42, 0x02, 0x81, 0x86, 0x12, 0x53 },
		0, 0, 0
	};
	/* we'll re-use the following for the replies */
	struct {
		uint32_t magic;
		uint32_t type;
		char handle[8];
		uint32_t from_filler;
		uint32_t from;
		uint32_t length;
	} __attribute__((packed)) request;

	/* set up socket */

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "E: creating socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 */
	addr.sin_port = htons(port);

	if(bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "E: binding to socket: %s\n", strerror(errno));
		close(sockfd);
		return -1;
	}

	fprintf(stderr, "I: waiting for connection on 127.0.0.1:%d\n", port);

	if(listen(sockfd, 0) < 0) {
		fprintf(stderr, "E: listening to socket: %s\n", strerror(errno));
		close(sockfd);
		return -1;
	}

	if((conn = accept(sockfd, NULL, NULL)) < 0) {
		fprintf(stderr, "E: accepting connection: %s\n", strerror(errno));
		close(sockfd);
		return -1;
	}

	result = -1;

	/* first thing is to send the nbd server message */
	memset(&initmsg.reserved, 0, sizeof(initmsg.reserved));
	initmsg.export_size = htonl(DEVICE_STORAGE_SIZE);
	write(conn, &initmsg, sizeof(initmsg));

	/* then wait for requests. we probably shouldn't need select(), though */
	while(1) {
		FD_ZERO(&fds);
		FD_SET(conn, &fds);
		ret = select(conn + 1, &fds, NULL, NULL, NULL);
		if(ret < 0) {
			fprintf(stderr, "E: error on select()ing: %s\n", strerror(errno));
			goto stop;
		} else if (ret > 0) {
			reads = read(conn, &request, sizeof(request));
			if(reads != sizeof(request)) {
				fprintf(stderr, "E: error on read()ing\n");
				goto stop;
			}
			if(request.magic != htonl(0x25609513)) {
				fprintf(stderr, "E: wrong request magic value\n");
				goto stop;
			}
			if(request.type == htonl(2)) {
				/* shut down connection */

				fprintf(stderr, "I: got shutdown command\n");
				request.magic = htonl(0x67446698);
				request.type = 0;
				write(conn, &request, 16); /* send OK */
				result = 0;
				goto stop;

			} else if(request.type == htonl(0)) {
				/* read */
				
				if(request.from_filler != 0) {
					fprintf(stderr, "E: got high memory (>4GB) access, not implemented.\n");
					goto stop;
				}
				fprintf(stderr, "I: got read request, source address 0x%08x, length 0x%08x\n", ntohl(request.from), ntohl(request.length));

				if(flash_dump_data(ntohl(request.from), ntohl(request.length), &buf)) {
					fprintf(stderr, "E: reading flash, will return an error.\n");
					request.magic = htonl(0x67446698);
					request.type = 1;
					write(conn, &request, 16); /* send OK */
				} else {
					request.magic = htonl(0x67446698);
					request.type = 0;
					write(conn, &request, 16); /* send OK */
					c = 0;
					while(c < ntohl(request.length)) {
						int todo = ntohl(request.length) - c;
						ret = write(conn, &buf[c], todo > 1024 ? 1024 : todo);
						if(ret == -1) {
							fprintf(stderr, "E: writing to socket\n");
							goto stop;
						}
						c += ret;
					}
					free(buf);
				}

			} else if(request.type == htonl(1)) {
				/* write */
				
				if(request.from_filler != 0) {
					fprintf(stderr, "E: got high memory (>4GB) access, not implemented.\n");
					goto stop;
				}
				fprintf(stderr, "I: got write request, target address 0x%08x, length 0x%08x\n", ntohl(request.from), ntohl(request.length));

				uint32_t length = ntohl(request.length);
				int one_go = MAX_CHUNK;
				int done, cont;

				buf = malloc(one_go);
				if(NULL == buf) {
					fprintf(stderr, "E: claiming buffer memory\n");
					goto stop;
				}

				done = 0;
				cont = 1;
				while(done < length) {
					if(length - done < one_go) {
						one_go = length - done;
						cont = 0;
					}
					c = 0;
					while(c < one_go) {
						ret = read(conn, &buf[c], (one_go - c) > 1024 ? 1024 : one_go - c);
						if(ret == -1) {
							fprintf(stderr, "E: reading from socket\n");
							goto stop;
						}
						c += ret;
					}
					if(flash_program_data(ntohl(request.from), buf, one_go, cont)) {
						fprintf(stderr, "E: flashing data\n");
						goto stop;
					}
					done += one_go;
				}

				free(buf);

				request.magic = htonl(0x67446698);
				request.type = 0;
				write(conn, &request, 16); /* send OK */

			} else {
				fprintf(stderr, "I: got unimplemented request type %u\n", ntohl(request.type));
				goto stop;
			}
		}
	}

stop:
	close(conn);
	close(sockfd);
	return result;
}

#endif // NBD_SERVER

/**
* function for parsing address parameters
*
* \param address_string the string expression to be parsed
* \return address as an uint32_t
*/
uint32_t parse_address(char* address_string) {
	if(!strcmp("partitiontable", address_string)) {
		return 0;
	} else if(!strcmp("header", address_string)) {
		return 0x00000400;
	} else if(!strcmp("uboot", address_string)) {
		return 0x00000c00;
	} else if(!strcmp("devid", address_string)) {
		return 0x00040c00;
	} else if(!strcmp("kernel", address_string)) {
		return 0x00041000;
	} else if(!strcmp("isiswf", address_string)) {
		return 0x00381000;
	} else if(!strcmp("rootfs", address_string)) {
		return 0x003c1000;
	} else return strtol(address_string, NULL, 0);
}

/**
* main routine
*
* \return 0 on success
*/
int main(int argc, char** argv) {
	int err, ret;
	uint16_t ack;
	uint32_t size;

	if(argc < 1) exit(-1); /* will never happen */
	if(argc < 3) goto usage;

	/* set up USB connection */
	err = libusb_init(&lusb_ctx);
	if(err) {
		fprintf(stderr, "E: initializing libusbx: %s\n", libusb_error_name(err));
		goto fail;
	}

	lusb_dev = libusb_open_device_with_vid_pid(lusb_ctx, IMX35_USBLOADER_VID, IMX35_USBLOADER_PID);
	if(NULL == lusb_dev) {
		fprintf(stderr, "W: cannot find device (is it in USB loader mode? are you root?) - waiting for it");
		/* wait for device to appear */
		while(lusb_dev == NULL) {
			sleep(1);
			fprintf(stderr, ".");
			lusb_dev = libusb_open_device_with_vid_pid(lusb_ctx, IMX35_USBLOADER_VID, IMX35_USBLOADER_PID);
		}
		fprintf(stderr, "\nI: success.\n");
	}

	fprintf(stderr, "I: found suitable device\n");

	/* claim interface */
	err = libusb_claim_interface(lusb_dev, IMX35_USBLOADER_IFACE);
	if(err) {
		fprintf(stderr, "E: claiming interface: %s\n", libusb_error_name(err));
		goto fail;
	}

	/*
	 * send a command that will be ACK'ed by the ROM kernel but
	 * replied to differently when the RAM kernel is running
	 */
	if(send_command("211", 0x0606, 0x02, 0x04)) goto fail;
	if(!read_reply(8, "2..4", &ack, &size)) {
		/* success here means we received 8 bytes (RAM kernel reply) */
		if(ack == 0) {
			/* device will also send an identifier if the flash was initialized before */
			if(size > 0) {
				if(read_reply(size, "")) goto fail;
			}
			/* skip RAM kernel setup */
			goto ramkernel_running;
		}
	} else {
		/* receiving 8 bytes failed, we try again and ask for 4 bytes and check if it's the ROM kernel answering */
		if(send_command("211", 0x0606, 0x02, 0x04)) goto fail;
		if(check_romkernel_status(ROM_KERNEL_STATUS_OK)) goto fail;
		/* yep, ROM kernel is running. */
	}

	/* info about error on ROM kernel: too short read, from the first status read_reply() above */
	fprintf(stderr, "I: above error can be ignored, it's due to the device being in ROM kernel mode\n");

	/* initialize SoC registers */
	if(init_registers()) {
		fprintf(stderr, "E: error when initializing SoC registers\n");
		goto fail;
	}

	/* upload RAM kernel */
	if(upload_ramkernel(argv[1])) {
		fprintf(stderr, "E: error when uploading ramkernel\n");
		goto fail;
	}

	/* start uploaded kernel */
	if(send_command("2", 0x0505)) goto fail;
	if(check_romkernel_status(0x88888888)) goto fail;

	/* that's it. */
	libusb_release_interface(lusb_dev, IMX35_USBLOADER_IFACE);
	libusb_close(lusb_dev);

	fprintf(stderr, "I: RAM kernel should be running now. Trying to re-open device: ");

	lusb_dev = NULL;
	/* wait for device, poll in 1 sec steps */
	while(lusb_dev == NULL) {
		sleep(1);
		fprintf(stderr, ".");
		lusb_dev = libusb_open_device_with_vid_pid(lusb_ctx, IMX35_USBLOADER_VID, IMX35_USBLOADER_PID);
	}
	fprintf(stderr, "\nI: got it.\n");

	/* re-claim interface */
	err = libusb_claim_interface(lusb_dev, IMX35_USBLOADER_IFACE);
	if(err) {
		fprintf(stderr, "E: claiming interface: %s\n", libusb_error_name(err));
		goto fail;
	}

ramkernel_running:
	/* from this point on we expect that we talk to the RAM kernel */

	if(!strcmp("reset", argv[2])) {

		fprintf(stderr, "I: resetting device.\n");

		if(send_command("22", 0x0606, 0x0201)) goto fail;
		/* device will be reset, should not return anything. */

	} else if(!strcmp("info", argv[2])) {

		fprintf(stderr, "I: read info\n");

		if(flash_init()) goto fail;
		if(flash_info()) goto fail;

	} else if(!strcmp("dump", argv[2])) {

		uint32_t address, length;

		if(argc < 6) {
			fprintf(stderr, "E: wrong syntax.\n");
			goto usage;
		}

		address = parse_address(argv[3]);
		if(address % 512) {
			fprintf(stderr, "E: address must be a multiple of 512 (0x200).\n");
			goto fail;
		}
		length = strtol(argv[4], NULL, 0);
		if(length % 512) {
			fprintf(stderr, "E: length must be a multiple of 512 (0x200).\n");
			goto fail;
		}

		if(flash_init()) goto fail;
		if(flash_dump(address, length, argv[5])) goto fail;

	} else if(!strcmp("program", argv[2])) {

		uint32_t address;

		if(argc < 5) {
			fprintf(stderr, "E: wrong syntax.\n");
			goto usage;
		}

		address = parse_address(argv[3]);
		if(address % 512) {
			fprintf(stderr, "E: address must be a multiple of 512 (0x200).\n");
			goto fail;
		}

		if(flash_init()) goto fail;
		if(flash_program(address, argv[4])) goto fail;

#ifdef NBD_SERVER
	} else if(!strcmp("nbd", argv[2])) {

		if(argc < 3) {
			fprintf(stderr, "E: wrong syntax.\n");
			goto usage;
		}

		if(flash_init()) goto fail;
		if(flash_nbd(strtol(argc >= 4 ? argv[3] : "12345", NULL, 0))) goto fail;
#endif

	} else {

		if(strcmp("setup", argv[2])!=0) {
			fprintf(stderr, "E: no such command: %s\n", argv[2]);
			goto usage;
		}

	}

	goto quit;

usage:
	fprintf(stderr,
		"USB pseudo-fastboot utility tailored for the Kindle Keyboard\n"
		"\n"
		"SYNTAX:\n"
		"\n"
		"\t%s <RAMKERNEL> <COMMAND>\n"
		"\n"
		"<RAMKERNEL> is an ATK ramkernel, e.g. mx35to2_mmc.bin\n"
		"<COMMAND> is one of the following:\n"
		"\n"
		"setup\n"
		"\twill only set up ram kernel (which is done automatically\n"
		"\tfor all other commands) and is only there for a quick check\n"
		"\tif the device can be put into ram kernel mode.\n"
		"\n"
		"reset\n"
		"\twill reset the USB loader (back into ROM kernel mode).\n"
		"\n"
		"info\n"
		"\tread flash info and device serial number / MAC address\n"
		"\n"
		"dump <address> <length> <outfile>\n"
		"\tread <length> bytes of flash memory, starting at <address>\n"
		"\tand write it to <outfile>\n"
		"\n"
		"program <address> <image>\n"
		"\twrite given image to flash at given address\n"
		"\n"
#ifdef NBD_SERVER
		"nbd [<port>]\n"
		"\twill present the device flash memory as a network block device\n"
		"\ton a single-threaded server on 127.0.0.1:<port>. If not given,\n"
		"\t<port> defaults to 12345.\n"
		"\n"
#endif
		"\n"
		"NOTE:\thexadecimal values for <address> or <length> must be\n"
		"\tprepended by \"0x\", e.g. 0x00041000.\n"
		"\tfor <address> you can also use the constants \"partitiontable\",\n"
		"\t\"header\", \"uboot\", \"devid\", \"kernel\", \"isiswf\" and \"rootfs\".\n"
		"\n"
		, argv[0]);

	ret = 1;
	goto quit;

fail:
	fprintf(stderr, "E: aborting. It is suggested you power-cycle the device.\n");
	ret = 1;

quit:
	if(lusb_dev != NULL) {
		libusb_release_interface(lusb_dev, IMX35_USBLOADER_IFACE);
		libusb_close(lusb_dev);
	}
	if(lusb_ctx != NULL) libusb_exit(lusb_ctx);
	exit(ret);
}

