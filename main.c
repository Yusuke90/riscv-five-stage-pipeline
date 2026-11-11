#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "assembler.h"
#include "cli_display.h"
#include "pipeline_state.h"
#include "branch_predictor.h"

#define DEFAULT_MEM_SIZE (1u << 20) /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull
#define DEFAULT_PREDICTOR "2bit"

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <program.s> [options]\n"
            "  --batch              run to completion with no interaction, then dump state\n"
            "                       (pipelined, unless --single-cycle is also given)\n"
            "  --single-cycle       use the single-cycle engine (the reference oracle)\n"
            "  --predictor NAME     branch predictor for the pipeline (default %s):\n",
            prog, DEFAULT_PREDICTOR);
    for (int i = 0; i < bp_count(); i++)
        fprintf(stderr, "                         %-10s %s\n",
                bp_at(i)->name, bp_at(i)->description);
    fprintf(stderr,
            "  --hazard stall|forward   hazard policy for the pipeline (default forward)\n"
            "  --trace              print one line per pipeline cycle\n"
            "  --max-cycles N       cycle limit for --batch mode (default %llu)\n"
            "  --mem-size N         memory size in bytes (default %u)\n",
            (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int batch_mode = 0;
    int single_cycle = 0;
    int pipeline_options = 0; /* --predictor / --hazard / --trace was given */
    int trace = 0;
    int forwarding = 1;
    const char *predictor_name = DEFAULT_PREDICTOR;
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size = DEFAULT_MEM_SIZE;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--batch") == 0)
        {
            batch_mode = 1;
        }
        else if (strcmp(argv[i], "--single-cycle") == 0)
        {
            single_cycle = 1;
        }
        else if (strcmp(argv[i], "--trace") == 0)
        {
            trace = 1;
            pipeline_options = 1;
        }
        else if (strcmp(argv[i], "--predictor") == 0 && i + 1 < argc)
        {
            predictor_name = argv[++i];
            pipeline_options = 1;
        }
        else if (strcmp(argv[i], "--hazard") == 0 && i + 1 < argc)
        {
            const char *h = argv[++i];
            if (strcmp(h, "forward") == 0)
                forwarding = 1;
            else if (strcmp(h, "stall") == 0)
                forwarding = 0;
            else
            {
                fprintf(stderr, "unknown hazard policy '%s' (use 'stall' or 'forward')\n", h);
                return 1;
            }
            pipeline_options = 1;
        }
        else if (strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc)
        {
            max_cycles = strtoull(argv[++i], NULL, 0);
        }
        else if (strcmp(argv[i], "--mem-size") == 0 && i + 1 < argc)
        {
            mem_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else
        {
            fprintf(stderr, "unrecognized option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    const BranchPredictor *bp = bp_find(predictor_name);
    if (!bp)
    {
        fprintf(stderr, "unknown predictor '%s'\n", predictor_name);
        usage(argv[0]);
        return 1;
    }

    /* 1. Bring up the ISA. Later labs add their own register_rv32m(),
     *    register_rv32f(), register_custom_ext(), etc. right here. */
    isa_registry_init();
    register_rv32i_base();

    /* 2. Set up memory and assemble the program into it. */
    Memory mem;
    if (memory_init(&mem, mem_size) != 0)
    {
        fprintf(stderr, "error: could not allocate %u bytes of simulated memory\n", mem_size);
        return 1;
    }

    uint32_t entry_point = 0;
    AsmError err;
    AsmStatus asm_status = assemble_file(program_path, &mem, &entry_point, &err);
    if (asm_status != ASM_OK)
    {
        fprintf(stderr, "assembly error");
        if (err.line_number > 0)
            fprintf(stderr, " (line %d)", err.line_number);
        fprintf(stderr, ": %s\n", err.message);
        memory_free(&mem);
        return 1;
    }

    printf("assembled '%s' ok. entry point = 0x%08x\n", program_path, entry_point);

    /* 3. Reset the CPU at the program's entry point and run it. */
    CPUState cpu;
    cpu_reset(&cpu, entry_point);

    if (single_cycle && batch_mode)
    {
        run_batch(&cpu, &mem, max_cycles);
    }
    else if (!single_cycle && (batch_mode || pipeline_options))
    {
        Pipeline p;
        pipeline_init(&p, forwarding, bp);
        p.trace = trace;
        pipeline_run(&p, &cpu, &mem, max_cycles);

        cpu.cycle_count = p.cycles; /* so print_status shows the pipeline's cycle count */

        printf("\n");
        print_registers(&cpu);
        print_status(&cpu);
        if (cpu.status == CPU_RUNNING)
            printf("  -> stopped: reached max cycle limit (%llu)\n", (unsigned long long)max_cycles);
        pipeline_print_stats(&p);
    }
    else
    {
        run_repl(&cpu, &mem);
    }

    memory_free(&mem);
    return 0;
}
