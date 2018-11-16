// SPDX-License-Identifier: BSD-3-Clause
/*
 * Pinebook Touchpad Firmware Writer
 *
 * Copyright (C) 2018 Vasily Khoruzhick <anarsoul@gmail.com>
 */

#include <errno.h>
#include <getopt.h>
#include <hidapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RETRIES 5
#define USB_DEVICE_VID 0x258a
#define USB_DEVICE_PID 0x000c

static char *firmware_file;
static bool do_read, do_write;
static long int request_size;

static void usage(int argc, char *argv[])
{
	fprintf(stderr, "Usage: %s [options]\n\n"
	       "-w file | --write file		Write firmware from file to the device\n"
	       "-r file | --read file		Read firmware from device to the file\n"
	       "-s size | --request_size size	Set feature request size (see documentation)\n"
	       "-h | --help		Print this message\n", argv[0]);
}

static const char short_options[] = "w:r:s:h";

static const struct option long_options[] = {
	{"write", required_argument, NULL, 'w'},
	{"read", required_argument, NULL, 'r'},
	{"request_size", required_argument, NULL, 's'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, 0, 0}
};

void options_init(int argc, char *argv[])
{
	for (;;) {
		int index;
		int c;

		c = getopt_long(argc, argv, short_options, long_options,
				&index);
		if (c < 0)
			break;

		switch (c) {
		case 0:	/* getopt_long() flag */
			break;
		case 'w':
		case 'r':
			if (firmware_file) {
				fprintf(stderr, "Read and write are mutually exclusive!\n\n");
				exit(EXIT_FAILURE);
				usage(argc, argv);
			}
			firmware_file = strdup(optarg);
			if (c == 'w')
				do_write = true;
			else
				do_read = true;
			break;
		case 's':
			request_size = strtol(optarg, NULL, 0);
			if (errno == ERANGE) {
				fprintf(stderr, "Invalid request size: %s\n\n", optarg);
				usage(argc, argv);
				exit(EXIT_FAILURE);
			}
			break;
		case 'h':
			usage(argc, argv);
			exit(EXIT_SUCCESS);
		default:
			usage(argc, argv);
			exit(EXIT_FAILURE);
		}
	}
}

int do_read_fw(hid_device *handle, unsigned char *data, long int data_lenght)
{
#define READ_BLOCK_SIZE 2048
	unsigned char report_data[request_size];
	unsigned char command[READ_BLOCK_SIZE + 2];
	int res;

	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x52;
	report_data[2] = 0x00;
	report_data[3] = 0x00;
	report_data[4] = data_lenght & 0xff;
	report_data[5] = (data_lenght >> 8) & 0xff;
	
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send read command\n");
		return res;
	}

	for (int i = 0; i < data_lenght / READ_BLOCK_SIZE; i++)
	{
		memset(command, 0, sizeof(command));
		command[0] = 0x06;
		command[1] = 0x72;

		res = hid_get_feature_report(handle, command, sizeof(command));
		if (res != sizeof(command)) {
			fprintf(stderr, "Failed to read back data: %d\n", res);
			return res;
		}
		usleep(10000);
		memcpy(data + i * READ_BLOCK_SIZE, command + 2, READ_BLOCK_SIZE);
	}

	return 0;
}

void read_fw(void)
{
	const long int data_lenght = 14 * 1024;
	unsigned char read_data[data_lenght];
	FILE *out;
	int res, data_left;
	ssize_t written;

	hid_device *handle;

	out = fopen(firmware_file, "wb");
	if (!out) {
		fprintf(stderr, "Failed to open %s for write\n", firmware_file);
		exit(EXIT_FAILURE);
	}


	handle = hid_open(USB_DEVICE_VID, USB_DEVICE_PID, NULL);
	if (!handle) {
		fprintf(stderr, "Failed to open device\n");
		goto err_out_file;
	}

	res = do_read_fw(handle, read_data, data_lenght);
	if (res) {
		fprintf(stderr, "Failed to read data\n");
		goto err_out;
	}

	data_left = data_lenght;
	do {
		written = fwrite(read_data + (data_left - data_lenght), 1, 1024, out);
		data_left -= written;
	} while (written && data_left);

	if (data_left) {
		fprintf(stderr, "Failed to write file, data left: %d\n", data_left);
		goto err_out;
	}

	hid_close(handle);
	return;

err_out:
	hid_close(handle);
err_out_file:
	fclose(out);
	exit(EXIT_FAILURE);
}

int do_write_serial_number(hid_device *handle)
{
	unsigned char report_data[request_size];
	uint16_t vid, pid, serial_num;
	int res;

	/* Set address and lenght */
	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x52;
	report_data[2] = 0x80;
	report_data[3] = 0xff;
	report_data[4] = 0x08;
	report_data[5] = 0x00;
	
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send 'set address and len' command\n");
		return res;
	}

	/* Read VID and PID */
	memset(report_data, 0, request_size);
	report_data[0] = 0x05;
	report_data[1] = 0x72;

	res = hid_get_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to read VID and PID\n");
		return res;
	}

	vid = report_data[2] << 8 | report_data[3];
	pid = report_data[4] << 8 | report_data[5];

	/* Read serial number */
	res = hid_get_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to read serial number\n");
		return res;
	}
	serial_num = report_data[4] << 8 | report_data[5];

	printf("VID: %.4x PID: %.4x Serial: %.4x\n", (int)vid, (int)pid, (int)serial_num);

	/* Erase this area */
	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x65;
	report_data[2] = 0xff;
	report_data[3] = 0x00;
	report_data[4] = 0x00;
	report_data[5] = 0x00;
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send erase command\n");
		return res;
	}
	usleep(200000);

	/* Write VID PID Serial number */
	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x57;
	report_data[2] = 0x80;
	report_data[3] = 0xff;
	report_data[4] = 0x08;
	report_data[5] = 0x00;
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send write command\n");
		return res;
	}

	/* First VID and PID */
	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x77;
	report_data[2] = (vid >> 8) & 0xff;
	report_data[3] = (vid & 0xff);
	report_data[4] = (pid >> 8) & 0xff;
	report_data[5] = (pid & 0xff);
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to write VID and PID\n");
		return res;
	}

	/* Then serial */
	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x77;
	report_data[2] = (1) & 0xff; /* m_sensor_direct */
	report_data[3] = 0x00;
	report_data[4] = (serial_num >> 8) & 0xff;
	report_data[5] = (serial_num & 0xff);
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to write VID and PID\n");
		return res;
	}

	return 0;
}

int do_write_fw(hid_device *handle, unsigned char *data, long int data_lenght)
{
	unsigned char report_data[request_size];
	unsigned char command[2050];
	int res;

	report_data[0] = 0x05; /* report id */
	report_data[1] = 0x57;
	report_data[2] = 0x00;
	report_data[3] = 0x00;
	report_data[4] = data_lenght & 0xff;
	report_data[5] = (data_lenght >> 8) & 0xff;
	
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send 1st write command\n");
		return res;
	}

	for (int i = 0; i < data_lenght / 2048; i++)
	{
		memset(command, 0, sizeof(command));
		command[0] = 0x06;
		command[1] = 0x77;
		memcpy(command + 2, data + i * 2048, 2048);
		/* FIXME: why? */
		if (i == 0)
			command[2] = 0x00;

		res = hid_send_feature_report(handle, command, sizeof(command));
		if (res != sizeof(command)) {
			fprintf(stderr, "Failed to write data\n");
			return res;
		}
		usleep(10000);
	}

	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send 2nd write command\n");
		return res;
	}

	memset(command, 0, sizeof(command));
	command[0] = 0x06;
	command[1] = 0x77;
	memcpy(command + 2, data, 2048);
	res = hid_send_feature_report(handle, command, sizeof(command));
	if (res != sizeof(command)) {
		fprintf(stderr, "Failed to write data\n");
		return res;
	}
	usleep(10000);

	return 0;
}

void write_fw(void)
{
	unsigned char report_data[request_size];
	long int data_lenght = 14 * 1024;
	unsigned char data[data_lenght];
	unsigned char read_data[data_lenght];
	FILE *in;
	int res;
	ssize_t offset = 0;
	int retries;

	hid_device *handle;

	in = fopen(firmware_file, "rb");
	if (!in) {
		fprintf(stderr, "Failed to open %s for read\n", firmware_file);
		exit(EXIT_FAILURE);
	}

	memset(data, 0, sizeof(data));
	while (!feof(in)) {
		ssize_t bytes = fread(data + offset, 1, 1024, in);
		if (!bytes)
			break;
		offset += bytes;
	}
	fclose(in);

	if (offset != data_lenght) {
		fprintf(stderr, "Short firmware: %d bytes\n", (int)offset);
		exit(EXIT_FAILURE);
	}

	handle = hid_open(USB_DEVICE_VID, USB_DEVICE_PID, NULL);
	if (!handle) {
		fprintf(stderr, "Failed to open device\n");
		exit(EXIT_FAILURE);
	}

	/* Erase pages 0-6 */
	memset(report_data, 0x45, request_size);
	report_data[0] = 0x05; /* report id */
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send erase command\n");
		goto err_out;
	}

	retries = RETRIES;
	do {
		if (!do_write_fw(handle, data, sizeof(data)))
			break;
		fprintf(stderr, "Failed to write firmware. Retrying... (%d attempts left)\n", retries);
	} while (retries--);

	retries = RETRIES;

	do {
		if (!do_read_fw(handle, read_data, sizeof(read_data))) {
			if (!memcmp(data, read_data, sizeof(data)))
				break;
			else
				fprintf(stderr, "Firmware read from device differs from written!\n");
		}
		fprintf(stderr, "Firmware comparison failed. Retrying... (%d attempts left)\n", retries);
	} while (retries--);

	if (!retries)
		goto err_out;

	/* Write serial number */
	res = do_write_serial_number(handle);
	if (res) {
		fprintf(stderr, "Failed to write serial number\n");
		goto err_out;
	}

	/* Send end programming command */
	memset(report_data, 0x55, request_size);
	report_data[0] = 0x05;
	res = hid_send_feature_report(handle, report_data, request_size);
	if (res != request_size) {
		fprintf(stderr, "Failed to send end programming\n");
		goto err_out;
	}

	hid_close(handle);
	return;

err_out:
	hid_close(handle);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	options_init(argc, argv);

	if (!request_size) {
		fprintf(stderr, "Request size is not specified!\n\n");
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}

	printf("Request size is %ld\n", request_size);

	if (do_read) {
		read_fw();
	} else if (do_write) {
		printf("You have 5 seconds to press CTRL+C\n");
		fflush(stdout);
		sleep(5);
		write_fw();
	} else {
		fprintf(stderr, "Neither read or write are specified!\n\n");
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}

	return 0;
}
