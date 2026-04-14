1. review part then we close app, and it resume run on next stage. Currently the ::exec() is reused, but probably custom QEventLoop should be used instead.
2. renderer debug information must be ON/OFF
