/*
* avplay.h
* ~~~~~~~~
*
* Copyright (c) 2011 Jack (jack.wgm@gmail.com)
*
*/

#ifndef AVLOGGER_H_
#define AVLOGGER_H_

#include <stdio.h>
#include <stdarg.h>

/*
* Set log to file.
* @param logfile write log to logfile file.
*/
int logger_to_file(const char* logfile);

/*
* Close log file.
*/
int close_logger_file();

/*
* Write formatted output to log.
* @param format string that contains the text to be written to log.
*/
int logger(const char *fmt, ...);

#endif /* AVLOGGER_H_ */
