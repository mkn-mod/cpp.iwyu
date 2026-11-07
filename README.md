# cpp.iwyu

**Compile phase module running [include-what-you-use](https://include-what-you-use.org) on project sources**

Each file is checked with the same flags its real compilation uses. iwyu's suggestions are printed,
they do not fail the build.

## Prerequisites

- [maiken](https://github.com/mkn/mkn)
- `iwyu` or `include-what-you-use` on `PATH`

## Usage

```yaml
mod:
- name: cpp.iwyu
  compile:
    args: -DSOME_DEFINE        # additional arguments
    headers: -x c++-header     # additional arguments for header files (h hh hpp hxx)
    inc: inc/1 inc/2           # additional include directories
    paths: search/dir          # also check files found in these directories
    ignore: src/python3 tpp    # skip files whose path contains any of these
    types: cpp hpp cc          # file types checked, default: cpp cxx cc h hpp
    fix: --nocomments          # apply the suggestions with fix_include, using these arguments
    system_last: true          # with fix, put system includes after project includes
```

Every key is optional, the project's own source files are always checked. Lists are space
separated, `types` may also be `:` separated.

## Applying suggestions

With `fix`, iwyu's output is written to `<build dir>/iwyu.out` and passed to iwyu's `fix_include`
(`fix_includes.py` upstream), which edits the files. `fix: ""` uses `fix_include`'s defaults. Useful arguments:

| argument | effect |
|---|---|
| `-n` | print the diffs, do not edit |
| `--nocomments` | no `// for X` comments after includes |
| `--nosafe_headers` | also remove includes from headers, by default headers only get additions |
| `--noreorder` | do not reorder existing includes, for order sensitive includes |
| `--ignore_re REGEX` | do not edit matching files |

`fix_include` puts system includes first. With `system_last: true` the module then moves them after
the project includes in every file it edited, so a system include cannot hide a project header
missing its own includes. Only contiguous runs of `#include` lines are reordered, blank line separated groups keep
their order, includes in `#if` sections stay where they are.

Edits happen during the compile phase, so the edited files are what get compiled. Running `fix`
from its own profile keeps it out of normal builds.

## Building

```bash
mkn clean build -dtOa "-std=c++20 -fPIC"
```

## Testing

Runs iwyu over this module's own source:

```bash
mkn clean build -dtOa "-std=c++20 -fPIC" -p iwyu
```
