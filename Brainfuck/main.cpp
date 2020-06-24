#include <string>
#include <vtil/vtil>

#pragma comment(linker, "/STACK:67108864")

void handle_inc(vtil::basic_block*& block);
void handle_dec(vtil::basic_block*& block);
void handle_print(vtil::basic_block*& block);
void handle_read(vtil::basic_block*& block);
void handle_te(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks);
void handle_tne(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks);
void handle_instruction(vtil::basic_block*& block, char instruction, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks);
void update_branch(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks);

void handle_inc(vtil::basic_block*& block)
{
    auto current_value = block->tmp(8);
    block->ldd(current_value, vtil::REG_SP, vtil::make_imm(0ull));
    block->add(current_value, 1);
    block->str(vtil::REG_SP, vtil::make_imm(0ull), current_value);
}

void handle_dec(vtil::basic_block*& block)
{
    auto current_value = block->tmp(8);
    block->ldd(current_value, vtil::REG_SP, vtil::make_imm(0ull));
    block->sub(current_value, 1);
    block->str(vtil::REG_SP, vtil::make_imm(0ull), current_value);
}

void handle_te(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks)
{
    auto [tmp, cond] = block->tmp(8, 1);
    block->ldd(tmp, vtil::REG_SP, 0);
    block->te(cond, tmp, 0);
    block->js(cond, ++vip, vtil::invalid_vip);

    blocks.push_back(block->entry_vip);
    block = block->fork(vip);
}

void handle_tne(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks)
{
    auto [tmp, cond] = block->tmp(8, 1);
    block->ldd(tmp, vtil::REG_SP, 0);
    block->tne(cond, tmp, 0);
    block->js(cond, block->entry_vip, ++vip);

    update_branch(block, vip, blocks);
    block = block->fork(vip);
}

void handle_print(vtil::basic_block*& block)
{
    block->ldd(x86_reg::X86_REG_AL, vtil::REG_SP, 0);
    block->vpinr(x86_reg::X86_REG_AL); // make sure this doesn't get optimized away
    block->vemit('.');
}

void handle_read(vtil::basic_block*& block)
{
    block->vemit(',');
    block->vpinw(x86_reg::X86_REG_AL); // make sure this doesn't get optimized away
    block->str(vtil::REG_SP, 0, x86_reg::X86_REG_AL);
}

void update_branch(vtil::basic_block*& block, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks)
{
    auto matching_vip = blocks.back(); blocks.pop_back();
    auto matching_block = block->owner->explored_blocks[matching_vip];
    matching_block->stream.back().operands[2].imm().u64 = vip;
    block->fork(block->entry_vip); // link the previously undefined block
}

void handle_instruction(vtil::basic_block*& block, char instruction, vtil::vip_t& vip, std::list<vtil::vip_t>& blocks)
{
    switch (instruction)
    {
        case '>':
            block->add(vtil::REG_SP, 1);
            break;
        case '<':
            block->sub(vtil::REG_SP, 1);
            break;
        case '+':
            handle_inc(block);
            break;
        case '-':
            handle_dec(block);
            break;
        case '[':
            handle_te(block, vip, blocks);
            break;
        case ']':
            handle_tne(block, vip, blocks);
        case '.':
            handle_print(block);
            break;
        case ',':
            handle_read(block);
            break;
        default:
            break;
    }
}

std::pair<std::string, std::optional<std::string>> handle_arguments(int argc, char* argv[])
{
    if(argc < 2)
    {
        vtil::logger::error(
                "%s\n%s\n",
                "Missing argument! Usage:",
                "Brainfuck.exe path_to_brainfuck_program.bf [path_to_output_vtil.vtil]");
    }
    else
    {
        std::ifstream stream(argv[1]);
        auto program = std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());

        std::optional<std::string> output = std::nullopt;
        if(argc == 3) output = std::make_optional(argv[2]);

        return {program, output};
    }
}

int main(int argc, char* argv[])
{
    auto [program, output] = handle_arguments(argc, argv);

    auto block = vtil::basic_block::begin(0x0);
    auto blocks = std::list<vtil::vip_t>();

    vtil::vip_t vip = 0;
    for(auto instruction : program)
    {
        handle_instruction(block, instruction, vip, blocks);
    }

    block->vexit(0ull);

    vtil::logger::log("Lifted! Running optimizations...\n\n");

    vtil::optimizer::apply_each<
        vtil::optimizer::profile_pass,
        vtil::optimizer::collective_pass
    >{}(block->owner);

    vtil::logger::log("\nOptimizations applied! Here's the VTIL:\n\n");

    vtil::debug::dump(block->owner);

    if(output)
    {
        vtil::logger::log("\nSaving VTIL to %s", output.value());
        vtil::save_routine(block->owner, output.value());
    }
}