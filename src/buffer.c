
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "buffer.h"

struct libxmp_buffer *libxmp_buffer_new(unsigned char *p, size_t size)
{
	struct libxmp_buffer *buf;

	if ((buf = malloc(sizeof (struct libxmp_buffer))) == NULL) {
		goto err;
	}

	buf->start = buf->pos = p;
	buf->end = buf->start + size;

	return buf;

    err:
	return NULL;
}

void libxmp_buffer_release(struct libxmp_buffer *buf)
{
	free(buf);
}

#define ASSIGN_ENDIAN(T,fmt,f) do {					\
	char e = *(fmt)++;						\
	T *b = va_arg(ap, T *);						\
	if (b != NULL) {						\
		if (e == 'l') {						\
			*b = (uint8)libxmp_buffer_read##f##l(buf);	\
		} else if (e == 'b') {					\
			*b = (uint8)libxmp_buffer_read##f##b(buf);	\
		}							\
	}								\
} while (0)

#define ASSIGN_VALUE(T,fmt,size) do {			\
	switch (size) {					\
	case 8:						\
		{ T *b = va_arg(ap, T *);		\
		*b = (T)libxmp_buffer_read8(buf);	\
		break; }				\
	case 16:					\
		ASSIGN_ENDIAN(T, (fmt), 16);		\
		break;					\
	case 24:					\
		ASSIGN_ENDIAN(T, (fmt), 24);		\
		break;					\
	case 32:					\
		ASSIGN_ENDIAN(T, (fmt), 32);		\
		break;					\
	}						\
} while (0)

int libxmp_buffer_left(struct libxmp_buffer *buf)
{
	return buf->end - buf->pos;
}

int libxmp_buffer_scan(struct libxmp_buffer *buf, char *fmt, ...)
{
	va_list ap;
	int size;
	char t;

	va_start(ap, fmt);

	while ((t = *fmt) != 0) {
		switch (*fmt++) {
		case 's':
			size = strtoul(fmt, &fmt, 10);
			if (libxmp_buffer_read(buf, va_arg(ap, void *), size) != size) {
				longjmp(buf->jmp, LIBXMP_BUFFER_ERANGE);
			}
			break;
		case 'b':
			size = strtoul(fmt, &fmt, 10);
			ASSIGN_VALUE(uint8, fmt, size);
			break;
		case 'w':
			size = strtoul(fmt, &fmt, 10);
			ASSIGN_VALUE(uint16, fmt, size);
			break;
		case 'd':
			size = strtoul(fmt, &fmt, 10);
			ASSIGN_VALUE(uint32, fmt, size);
			break;
		default:
			goto err;
		}

		if (*fmt == ';') {
			fmt++;
		} else if (*fmt != 0) {
			goto err;
		}
	}

	va_end(ap);
	return 0;

    err:
	va_end(ap);
	return -1;
}

int libxmp_buffer_read(struct libxmp_buffer *buf, void *dst, size_t size)
{
	/* check range */
	if (buf->pos + size >= buf->end) {
		size = buf->end - buf->pos;
	}

	memcpy(dst, buf->pos, size);

	return size;
}

#define CHECK_RANGE(b,x) do {					\
	if ((x) >= (b)->end || (x) < (b)->start) {		\
		longjmp((b)->jmp, LIBXMP_BUFFER_ERANGE);	\
	}							\
	(b)->pos = (x);						\
} while (0)

void libxmp_buffer_seek(struct libxmp_buffer *buf, long offset, int whence)
{
	switch (whence) {
	case SEEK_SET:
		CHECK_RANGE(buf, buf->start + offset);
		break;
	case SEEK_CUR:
		CHECK_RANGE(buf, buf->pos + offset);
		break;
	case SEEK_END:
		CHECK_RANGE(buf, buf->end - offset - 1);
		break;
	default:
		longjmp(buf->jmp, LIBXMP_BUFFER_EINVAL);
	}
}

long libxmp_buffer_tell(struct libxmp_buffer *buf)
{
	return buf->pos - buf->start;
}


#define CHECK_SIZE(b,x) do {					\
	if ((b)->pos + (x) >= (b)->end) {			\
		longjmp((b)->jmp, LIBXMP_BUFFER_ERANGE);	\
	}							\
} while (0)

uint8 libxmp_buffer_read8(struct libxmp_buffer *buf)
{
	CHECK_SIZE(buf, 1);

	return *buf->pos++;
}

uint16 libxmp_buffer_read16l(struct libxmp_buffer *buf)
{
	uint16 a, b;

	CHECK_SIZE(buf, 2);

	a = (uint16)*buf->pos++;
	b = (uint16)*buf->pos++;

	return (b << 8) | a;
}

uint16 libxmp_buffer_read16b(struct libxmp_buffer *buf)
{
	uint16 a, b;

	CHECK_SIZE(buf, 2);

	a = (uint16)*buf->pos++;
	b = (uint16)*buf->pos++;

	return (a << 8) | b;
}

uint32 libxmp_buffer_read24l(struct libxmp_buffer *buf)
{
	uint32 a, b, c;

	CHECK_SIZE(buf, 3);

	a = (uint32)*buf->pos++;
	b = (uint32)*buf->pos++;
	c = (uint32)*buf->pos++;

	return (c << 16) | (b << 8) | a;
}

uint32 libxmp_buffer_read24b(struct libxmp_buffer *buf)
{
	uint32 a, b, c;

	CHECK_SIZE(buf, 3);

	a = (uint32)*buf->pos++;
	b = (uint32)*buf->pos++;
	c = (uint32)*buf->pos++;

	return (a << 16) | (b << 8) | c;
}

uint32 libxmp_buffer_read32l(struct libxmp_buffer *buf)
{
	uint32 a, b, c, d;

	CHECK_SIZE(buf, 4);

	a = (uint32)*buf->pos++;
	b = (uint32)*buf->pos++;
	c = (uint32)*buf->pos++;
	d = (uint32)*buf->pos++;

	return (d << 24) | (c << 16) | (b << 8) | a;
}

uint32 libxmp_buffer_read32b(struct libxmp_buffer *buf)
{
	uint32 a, b, c, d;

	CHECK_SIZE(buf, 4);

	a = (uint32)*buf->pos++;
	b = (uint32)*buf->pos++;
	c = (uint32)*buf->pos++;
	d = (uint32)*buf->pos++;

	return (a << 24) | (b << 16) | (c << 8) | d;
}

