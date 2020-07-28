# cpp.iwyu

** Compile stage plugin to to run include-what-you-use on all applicable files **

[![Travis](https://travis-ci.org/mkn-mod/cpp.iwyu.svg?branch=master)](https://travis-ci.org/mkn-mod/cpp.iwyu)

## Prerequisites
  [maiken](https://github.com/Dekken/maiken)

## Usage

```yaml
mod:
- name: cpp.iwyu
  compile:
    headers: -x c++-header      # h files need this for scanning if they are C++
    args: -std=c++17            # additional arguments
    inc: inc/1 inc/2            # additional include directories
    paths: search/dir           # scan directories
    ignore: src/python3         # if file found has path that contains string ignore



```

## Building

  *nix gcc:

    mkn clean build -dtOa "-fPIC" -l "-pthread -ldl"


## Testing

  *nix gcc:

    mkn clean build -dtOa "-fPIC" -l "-pthread -ldl" -p test run
