/*
 * avplay.h
 * ~~~~~~~~
 *
 * Copyright (c) 2011 Jack (jack.wgm@gmail.com)
 *
 */

#ifndef AVLOGGER_H_
#define AVLOGGER_H_

#ifdef _MSC_VER
#	include <windows.h>
#	define inline
#	define __CRT__NO_INLINE
#	ifdef API_EXPORTS
#		define EXPORT_API __declspec(dllexport)
#	else
#		define EXPORT_API __declspec(dllimport)
#	endif
#else
#	define EXPORT_API
#endif

#include <pthread.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <assert.h>
#include "globals.h"

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * Set log to file.
 * @param logfile write log to logfile file.
 */
EXPORT_API int logger_to_file(const char* logfile);

/*
 * Close log file.
 */
EXPORT_API int close_logger_file();

/*
 * Write formatted output to log.
 * @param format string that contains the text to be written to log.
 */
EXPORT_API int logger(const char *fmt, ...);

#ifdef  __cplusplus
}
#endif

#endif /* AVLOGGER_H_ */
