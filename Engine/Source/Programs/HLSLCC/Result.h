#pragma once

enum Result
{
    // no error
    OK = 0,
    Success = OK,

    // Compiler failures for cannot open file
    CompilerFailForCannotOpenFile,

    // Compiler failures for cannot find entry points
    CompilerFailForCannotFindEntryPoints,
};
