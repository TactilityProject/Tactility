#pragma once

/**
 * One function per shell builtin (see Tactility/Source/app/shell/commands/), referenced by
 * Shell.cpp's COMMANDS[] table.
 */

int cmdHelp(int argc, char** argv);
int cmdWhich(int argc, char** argv);
int cmdLs(int argc, char** argv);
int cmdCat(int argc, char** argv);
int cmdHead(int argc, char** argv);
int cmdTail(int argc, char** argv);
int cmdWc(int argc, char** argv);
int cmdMkdir(int argc, char** argv);
int cmdRm(int argc, char** argv);
int cmdCp(int argc, char** argv);
int cmdMv(int argc, char** argv);
int cmdTouch(int argc, char** argv);
int cmdDf(int argc, char** argv);
int cmdDu(int argc, char** argv);
int cmdSh(int argc, char** argv);
int cmdDate(int argc, char** argv);
int cmdPrintf(int argc, char** argv);
int cmdClear(int argc, char** argv);
int cmdFree(int argc, char** argv);
