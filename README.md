# Virtual Disk File System (foosh)

A FAT-based virtual file system implemented in shared memory, with a custom interactive shell for file and directory operations. Built as an Operating Systems assignment.

## Overview

The project consists of two programs:

- **`diskmanager`** — Creates and destroys the virtual disk in System V shared memory.
- **`foosh`** — An interactive shell that attaches to the virtual disk and provides file system commands (create directories, copy files, list contents, etc.).

## Building

```bash
make
```

This compiles both `diskmanager` and `foosh`. Requires GCC and a POSIX-compatible system (Linux).

To clean build artifacts:

```bash
make clean
```

## Usage

### 1. Create the Virtual Disk

```bash
./diskmanager create
```

This allocates a 64 MB shared memory segment and initializes the disk layout (superblock, bitmap, FAT, root directory).

### 2. Launch the Shell

```bash
./foosh
```

You'll see a prompt like:

```
[foosh] VD:>
```

### 3. Shell Commands

| Command | Usage | Description |
|---|---|---|
| `cd` / `chdir` | `cd path` | Change directory (supports `.`, `..`, absolute & relative paths). `cd` with no argument returns to root. |
| `mkdir` / `md` | `mkdir dirname` | Create a new directory. |
| `dir` | `dir` | Compact listing of the current directory (names in columns). |
| `ls` | `ls [path]` | Detailed listing showing type, name, size, and first block number. |
| `cp` / `copy` | `cp src dst` | Copy a file (see below for host ↔ VD syntax). |
| `prn` / `type` | `prn filename` | Print a virtual disk file's contents to the terminal. |
| `exit` / `quit` | `exit` | Exit the shell. |

#### Copying Files Between Host and Virtual Disk

The `cp` command uses a backtick (`` ` ``) prefix to denote host (real) file system paths:

```
# Copy a host file INTO the virtual disk
cp `/home/user/hello.txt hello.txt

# Copy a virtual disk file OUT to the host
cp hello.txt `/home/user/hello_copy.txt

# Copy within the virtual disk
cp hello.txt backup.txt
```

### 4. Remove the Virtual Disk

When finished, destroy the shared memory segment:

```bash
./diskmanager remove
```

## Disk Layout

The virtual disk is 64 MB (65,536 blocks × 1,024 bytes/block), organized as:

```
Block(s)       Purpose
─────────────────────────────────
0              Superblock (total blocks, free count, root block number)
1 – 8          Free-space bitmap (1 bit per block)
9 – 264        FAT (File Allocation Table, 4 bytes per entry)
265            Root directory
266 – 65535    Data blocks
```

### Superblock (Block 0)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 bytes | Total number of blocks |
| 4 | 4 bytes | Number of free blocks |
| 8 | 4 bytes | Root directory block number |

### Directory Entry (`struct metadata`, 32 bytes)

| Field | Size | Description |
|-------|------|-------------|
| `type` | 1 byte | `'d'` for directory, `'f'` for file |
| `name` | 23 bytes | Null-terminated name |
| `size` | 4 bytes | File size in bytes, or entry count for directories |
| `firstblock` | 4 bytes | First block number in the FAT chain |

Each block holds up to 32 directory entries. Directories always begin with `.` (self) and `..` (parent) entries.

## Architecture

```
┌──────────────┐         ┌────────────────────────────────────┐
│ diskmanager  │──create──▶  Shared Memory (System V IPC)     │
│              │──remove──▶  64 MB Virtual Disk               │
└──────────────┘         └────────────┬───────────────────────┘
                                      │ shmat()
                              ┌───────▼───────┐
                              │    foosh      │
                              │  (shell)      │
                              │               │
                              │  diskutils.c  │
                              │  (fs layer)   │
                              └───────────────┘
```

### File Structure

| File | Description |
|------|-------------|
| `vdisk.h` | Shared constants and the `struct metadata` definition |
| `diskmanager.c` | Creates/removes the shared memory virtual disk |
| `diskutils.c` | File system operations (bitmap, FAT, directories, copy, path resolution) |
| `foosh.c` | Interactive shell — command parsing and dispatch |
| `makefile` | Build rules |

## Design Notes

- **FAT-based chaining** — Files spanning multiple blocks are linked via the File Allocation Table, similar to FAT12/16 file systems.
- **Random block allocation** — Free blocks are selected randomly rather than sequentially, simulating fragmentation behavior.
- **Shared memory IPC** — The virtual disk lives in a System V shared memory segment (`shmget`/`shmat`), keyed via `ftok("/tmp", 'V')`. This allows the disk to persist across shell sessions until explicitly removed.
- **No journaling or crash recovery** — The file system does not handle unexpected termination gracefully; always use `diskmanager remove` to clean up.
