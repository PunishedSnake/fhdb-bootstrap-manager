#ifndef PS2_HDD_BOOTSTRAP_MANAGER_SESSION_LOG_H
#define PS2_HDD_BOOTSTRAP_MANAGER_SESSION_LOG_H

void session_log_line(const char *format, ...);
int session_log_flush(unsigned int storage);

#endif
