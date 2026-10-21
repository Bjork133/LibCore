# scrypt-sha512

An scrypt implementation without external dependencies (i.e. OpenSSL) that uses the SHA512 hash function.

## Introduction

First of all credit where credit's due:
* Stanford Javascript Crypto Library http://bitwiseshiftleft.github.io/sjcl/
* Colin Percival http://www.tarsnap.com/scrypt.html
* Olivier Gay https://github.com/ogay/hmac/

Current scrypt implementations utilize PBKDF2-HMAC-SHA256 (as they should!). The goal was to make the algorithm plugable and support in the future other hash or mix functions (like SHA3 and ChaCha20). Another goal was to remove all external library dependencies and provide a solution that included all the source code. 

## Building

First install make and gcc:

```bash
sudo apt install make gcc
```

Then clone the repository and execute make:

```bash
git clone https://github.com/enceeper/scrypt
cd scrypt
make
```

## Usage

For simplicity the parameters **p** and **r** are hardcoded in the software to 1 and 8 respectively. Only N can be set from the command line. The first parameter is the HEX encoded salt and the second parameter is the HEX encoded password. Please make sure that you normalize the password (using NFKD form) in order to produce consistent results.

```bash
scrypt 4751535a1c65ef8c 662336d127d8ff74 32768
```

## Copyright and license

Copyright 2018 Vassilis Poursalidis. Released under GNU GPL3 or later - see the `LICENSE` file for details.
