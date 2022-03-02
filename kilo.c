#include <termio.h>
#include <unistd.h>

int main() {
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
