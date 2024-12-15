# RCOM PROJECT 2: FTP CLIENT

## Authors

- Filipe Gonçalves Neves Gaio (up202204985)
- Henrique Sardo Fernandes (up202204988)

## How to run

### Using the Makefile

1. Edit the `Makefile` with the desired FTP server address. Some examples are already provided. (Notes on the local ftp server can be found below).
2. Run with `make run`.
3. The file will be downloaded to the current directory with the same name as the original file.

### Using the executable

1. Compile the project with `make all`.
2. Run the client with `./bin/download ftp://[user[:password]@]host[:port]/path`.
3. The file will be downloaded to the current directory with the same name as the original file.

## Local FTP server

1. Navigate to the `ftp-server` directory
2. Create a new virtual environment with `python3 -m venv env`
3. Activate the virtual environment with `source env/bin/activate`
4. Install the required packages with `pip install -r requirements.txt`
5. Run the server with `launch-server.sh`. The server is running on port `2121`, the username is `user` and the password is `12345`. It is also possible to access the server with the `anonymous` user.
6. The `Makefile` includes examples for the local server, and as a bonus, the command `make diff` can be used to compare the downloaded file with the original one.

> Inside the `ftp-server` directory there is a directory called `FTP` that contains the files that can be downloaded. The contents of this directory can be modified to test the client with different files.
