#include <stdio.h>
#include <time.h>

#include "avlogger.h"

static FILE *logfp = NULL;
static int log_ref = 0;

int logger_to_file(const char* logfile)
{
	if (log_ref++ == 0)
	{
		logfp = fopen(logfile, "w+b");
		if (!logfp)
		{
			log_ref--;
			return -1;
		}
	}
	return 0;
}

int close_logger_file()
{
	if (!logfp)
		return -1;

	if (--log_ref == 0)
	{
		fclose(logfp);
		logfp = NULL;
	}

	return 0;
}

/* 内部日志输出函数实现.	*/
static
void get_current_time(char *buffer)
{
	struct tm current_time;
	time_t tmp_time;

	time(&tmp_time);

	current_time = *(localtime(&tmp_time));

	if (current_time.tm_year > 50)
	{
		sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
			current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday,
			current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
	}
	else
	{
		sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
			current_time.tm_year + 2000, current_time.tm_mon + 1, current_time.tm_mday,
			current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
	}
}

int logger(const char *fmt, ...)
{
	char buffer[65536];
	char time_buf[1024];
	va_list va;
	int ret = 0;

	va_start(va, fmt);
	vsprintf(buffer, fmt, va);

	get_current_time(time_buf);

	// 输出到屏幕.
	ret = printf("[%s] %s", time_buf, buffer);

	// 输出到文件.
	if (logfp)
	{
		fprintf(logfp, "[%s] %s", time_buf, buffer);
		fflush(logfp);
	}

	va_end(va);

	return ret;
}
