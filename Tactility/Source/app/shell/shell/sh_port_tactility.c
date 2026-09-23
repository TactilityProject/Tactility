/*
 * sh_port_tactility.c - platform implementation of the shell core's port contract.
 *
 * Replaces upstream's sh_port_esp.c, which routes external commands through breezybox_run_argv()
 * (the esp_console registry plus its ELF loader) and keeps its cwd in breezy_vfs. Neither exists
 * here: commands live in Shell's own table and the working directory belongs to ShellFs.
 *
 * sh_redir.c is used unchanged: it implements redirection by swapping the stdin/stdout/stderr
 * FILE* lvalues rather than juggling file descriptors, which works as long as stdio actually points
 * at the terminal app running this one. app_start_for_result_with_streams() arranges that, piping
 * this app's fd 0/1/2 before its task begins.
 */

#include <Tactility/app/shell/shell/sh_port.h>

#include <Tactility/app/shell/ShellBridge.h>

#include <stdio.h>
#include <string.h>

int sh_port_run_external(int argc, char** argv, int* found)
{
    return shell_bridge_run_command(argc, argv, found);
}

void sh_port_tmpfile(int which, char* buf, int bufsz)
{
    /*
     * Pipelines are implemented by staging one command's output in a file and feeding it to the
     * next, so this needs a writable location. The shell's own data directory is used rather than
     * the working directory: the cwd may be a read-only mount, or somewhere the user would rather
     * not have scratch files appear.
     *
     * `which` is ignored in favour of an internal counter: exec_pipe(), redir_begin() (here-docs)
     * and command_subst() each compute their own `which` independently, with overlapping values
     * (e.g. a here-doc and the outermost command substitution both use 2), so two of these nested
     * together could collide on the same file. No caller re-derives a path by calling this again
     * with the same `which` - each stores the returned buffer and reuses that - so nothing depends
     * on `which` itself, only on every call getting a distinct path.
     */
    (void)which;
    static int next_id = 0;
    shell_bridge_temp_path(next_id++, buf, bufsz);
}

int sh_port_chdir(const char* path)
{
    return shell_bridge_chdir(path);
}

void sh_port_getcwd(char* buf, int bufsz)
{
    shell_bridge_getcwd(buf, bufsz);
}
