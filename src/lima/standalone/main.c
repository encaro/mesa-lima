/* Author(s):
 *   Connor Abbott
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include "shader.h"

#define USAGE \
"usage: limasc -t [vert|frag] -o [output] input \n" \
"\n" \
"options:\n" \
"\t--type (-t) [vert|frag] -- choose which kind of shader\n" \
"\t--dump-asm (-d) -- print out the resulting assembly\n" \
"\t--syntax [verbose|explicit|decompile] -- " \
"choose the syntax for the disassembly\n\n" \
"\t\tFor vertex shaders: verbose will dump the raw fields, with\n" \
"\t\tlittle interpretation except for suppressing certain fields\n" \
"\t\twhen they are unused. Explicit will print a more normal\n" \
"\t\tassembly, but due to the nature of the instruction set it\n" \
"\t\twill still be verbose and unreadable. Decompile will try to\n" \
"\t\tproduce a more readable output at the expense of losing some\n" \
"\t\tdetails (such as how efficient the assembly is).\n\n" \
"\t\tFor fragment shaders: verbose will print out a more readable\n" \
"\t\tassembly, but sometimes it will be less clear which instructions\n" \
"\t\tare scheduled in which unit. The explicit syntax is more\n" \
"\t\tassembly-like and easier to parse, but at the expense of being\n" \
"\t\tless readable. Decompile is invalid for fragment shaders.\n\n" \
"\t\tExplicit is the default for vertex shaders, while verbose is the \n" \
"\t\tdefault for fragment shaders.\n\n" \
"\t--output (-o) -- the output file. Defaults to out.mbs\n" \
"\t--help (-h) -- print this message and quit.\n"

void usage(void)
{
	fprintf(stderr, USAGE);
}

int main(int argc, char** argv)
{
	bool dump_asm = false;
	lima_shader_stage_e stage = lima_shader_stage_unknown;
	lima_asm_syntax_e syntax = lima_asm_syntax_unknown;
	char* outfile = NULL;
	char* infile = NULL;
	
	static struct option long_options[] = {
		{"type",     required_argument, NULL, 't'},
		{"dump-asm", no_argument,       NULL, 'd'},
		{"syntax",   required_argument, NULL, 's'},
		{"output",   required_argument, NULL, 'o'},
		{"help",     no_argument,       NULL, 'h'},
		{0, 0, 0, 0}
	};
	
	while (true)
	{
		int option_index = 0;
		
		int c = getopt_long(argc, argv, "t:ds:o:h", long_options, &option_index);
		
		if (c == -1)
			break;
		
		switch (c)
		{
			case 't':
				if (strcmp(optarg, "vert") == 0)
					stage = lima_shader_stage_vertex;
				else if (strcmp(optarg, "frag") == 0)
					stage = lima_shader_stage_fragment;
				else
				{
					fprintf(stderr, "Error: unknown shader type %s\n", optarg);
					usage();
					exit(1);
				}
				break;
			
			case 'd':
				dump_asm = true;
				break;
				
			case 's':
				if (strcmp(optarg, "explicit") == 0)
					syntax = lima_asm_syntax_explicit;
				else if (strcmp(optarg, "verbose") == 0)
					syntax = lima_asm_syntax_verbose;
				else if (strcmp(optarg, "decompile") == 0)
					syntax = lima_asm_syntax_decompile;
				else
				{
					fprintf(stderr, "Error: unknown assembly syntax %s\n",
							optarg);
					usage();
					exit(1);
				}
				break;
				
			case 'o':
				if (outfile)
				{
					fprintf(stderr, "Error: output file specified more than once");
					usage();
					exit(1);
				}
				outfile = optarg;
				break;
				
			case 'h':
				usage();
				exit(0);
				
			case '?':
				usage();
				exit(1);
				break;
			
			default:
				abort();
		}
	}
	
	if (stage == lima_shader_stage_unknown)
	{
		fprintf(stderr, "Error: no shader type specified\n");
		usage();
		exit(1);
	}
	
	if (syntax == lima_asm_syntax_unknown)
	{
		switch (stage)
		{
			case lima_shader_stage_vertex:
				syntax = lima_asm_syntax_explicit;
				break;
			
			case lima_shader_stage_fragment:
				syntax = lima_asm_syntax_verbose;
				break;
				
			default:
				abort();
		}
	}
	
	if (optind == argc)
	{
		fprintf(stderr, "Error: no input specified\n");
		usage();
		exit(1);
	}
	
	if (optind < argc - 1)
	{
		fprintf(stderr, "Error: more than one input specified\n");
		usage();
		exit(1);
	}
	
	infile = argv[optind];
	if (!outfile)
		outfile = "out.mbs";
	
	return 0;
}
