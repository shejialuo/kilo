#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios originalTermios;

void die(const char* s) {

  write(STDOUT_FILENO, "\x1b[2J",4);
  write(STDOUT_FILENO, "\x1b[H",3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if(tcgetattr(STDIN_FILENO, &originalTermios) == -1) die("tcgetattr");
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

    There is an `ICANON` flag that allows us to turn off
    canonical mode. This means we will finally by reading
    input *byte-byte*, instead of line-by-line.

    `ISIG` turns off `Ctrl-C` and `Ctrl-Z`.

    `IXON` disables `Ctrl-S` and `Ctrl-Q`.

    `IEXTEN` disables `Ctrl-V`.
  */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /*
    The terminal translates each newline `"\n"` we print
    into a carriage return followed by a newline `"\r\n"`.
    The terminal requires both of these characters in
    order to start a new line of text. The carriage return
    moves the cursor down a line, scrolling the screen if
    necessary.

    We will turn off all ouput processing features by
    turning off the `OPOST` flag.
  
  */
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN |ISIG);

  /*
    The `VMIN` value sets the minimum number of bytes
    of input needed before `read()` can return.

    The `VTIME` value sets the maximum amount of time
    to wait before `read()` returns. It is in tenths of
    a second

    So we can simulate the animatation.
  */
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

  atexit(disableRawMode);
}

char editorReadKey() {
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if(nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

void editorRefreshScreen() {
  /*
    \x1b is the escape character, or 27 in decimal
    We are writing an *escape sequence* to the terminal.
    Escape sequences always start with an escape character
    followed by a `[` character.
  */
  write(STDOUT_FILENO, "\x1b[2J",4);
  // Move cursor position
  write(STDOUT_FILENO, "\x1b[H",3);
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J",4);
      write(STDOUT_FILENO, "\x1b[H",3);
      exit(0);
      break;
  }
}

int main() {

  enableRawMode();

  /*
    Now, the terminal starts in canonical mode, in this
    mode, keyboard input is only sent to your program
    when the user presses `enter`. However, we want to
    process each keypress as it comes in, so we can respond
    to it immediately.

    What we want is raw mode.
  */
  while(1) {
    /*
      This is a very useful snippet. It shows us how various
      keypresses translate into the bytes we read.

      You'll notice a few interesting things:
      + Arrow keys, `Page Up`, `Page Down`, `Home` and `End`
        all input 3 or 4 bytes to the terminal: `27`, `'['`,
        and then one or two other characters. This is known
        as an *escape sequence* All secape sequences start
        with a `27` byte. Pressing `Escape` sends a single
        `27` byte as input.
      + `Backspace` is byte `127`. `Delete` is a 4-byte
        escape sequence.
      + `Enter` is byte `10`, which is a newline character
      + The `ctrl` key combinations that do work seem to
        map the letters A-Z to the codes 1-26. 
    */
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
