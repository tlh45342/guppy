app_printf foundation regression

This intentionally tiny test proves the first application-output layer.
The existing logfile command currently uses the guppy_printf compatibility
entry point, which now delegates to app_vprintf().

Expected app-printf.log evidence includes:
  Logging to app-printf.log
  >> logfile
  logfile: app-printf.log
  >> logfile off
  Logging stopped.

This test does not migrate any normal Guppy command family yet.
