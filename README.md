# MyShell

A simple Unix‑like shell written in C, featuring process management, pipelines, history expansion, and basic job control.

## Features

* **Command Execution**: Run external programs with arguments.
* **Input/Output Redirection**: Use `>`, `>>`, and `<` to redirect streams.
* **Pipelines**: Support for a single pipe (`cmd1 | cmd2`).
* **Built‑in Commands**:

  * `cd <path>` — change the working directory.
  * `quit` — exit the shell.
  * `procs` — list active and suspended child processes.
  * `halt <pid>` — send `SIGSTOP` to pause a process.
  * `wakeup <pid>` — send `SIGCONT` to resume a process.
  * `ice <pid>` — send `SIGINT` (Ctrl‑C) to terminate a process.
  * `hist` — display the last 20 commands entered.
* **History Expansion**:

  * `!!` — repeat the last command.
  * `!n` — repeat the nth command from history.
* **Debug Mode**: Run the shell with `-d` to print internal debug messages (e.g., PIDs and errors).

## Requirements

* **Compiler**: GCC (or any C99‑compatible compiler)
* **Platform**: Linux/Unix (uses POSIX APIs)
* **Files**:

  * `main.c` — shell implementation and loop.
  * `LineParser.c` / `LineParser.h` — utility for parsing command lines.

## Compilation

```bash
gcc -std=gnu99 -D_XOPEN_SOURCE=600 -Wall -Wextra -o mysh main.c LineParser.c
```

* `-D_XOPEN_SOURCE=600` enables POSIX extensions (e.g., `WCONTINUED` for job control).
* Adjust filenames if your source files differ.

## Usage

```bash
./mysh [-d]
```

* `-d` — enable debug mode.
* The prompt shows the current working directory.

### Examples

```shell
/home/user: ls -l > out.txt    # redirect output
/home/user: cat out.txt | grep txt  # use a pipeline
/home/user: hist               # view command history
/home/user: !3                 # re‑run the 3rd command in history
/home/user: halt 1234          # suspend process with PID 1234
/home/user: wakeup 1234        # resume that process
/home/user: ice 1234           # send SIGINT to it
```

## Project Structure

```
├── LineParser.c
├── LineParser.h
├── main.c
└── README.md
```

## Author

Tomer R.

GitHub: [https://github.com/yourusername](https://github.com/TomerKBAB)

Feel free to fork, improve, and submit issues or pull requests!
