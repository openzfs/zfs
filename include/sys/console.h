#ifndef	_SPL_CONSOLE_H
#define	_SPL_CONSOLE_H

void
console_vprintf(const char *fmt, va_list args)
{
        vprintk(fmt, args);
}

void
console_printf(const char *fmt, ...)
{
        va_list args;

        va_start(args, fmt);
        console_vprintf(fmt, args);
        va_end(args);
}

#endif /* _SPL_CONSOLE_H */
