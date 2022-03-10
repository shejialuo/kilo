#include <stdlib.h>
#include <termio.h>
#include <unistd.h>

struct termios originalTermios;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &originalTermios);
  struct termios raw = originalTermios;
  /*
    The `ECHO` feature causes each key you type to be
    printed to the terminal, so you can see what you're
    typing. This is useful in canonical mode, but really
    gets in the way when we are trying to carefully render
    a user interface in raw mode.

    You may be familiar with this mode if you've ever had to
    type a password at the terminal, when using `sudo`
    for example
  */
  raw.c_lflag &= ~(ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  atexit(disableRawMode);
}

int main() {

  enableRawMode();

  char c;
  /*
    Now, the terminal starts in canonical mode, in this
    mode, keyboard input is only sent to your program
    when the user presses `enter`. However, we want to
    process each keypress as it comes in, so we can respond
    to it immediately.

    What we want is raw mode.
  */
  while(read(STDIN_FILENO, &c, 1) == 1 && c!= 'q');
  return 0;
}
