#include <drivers/pi4ioe5v6408.h>
#include <tactility/module.h>

const struct ModuleSymbol pi4ioe5v6408_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_direction),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_output_level),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_output_high_impedance),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_input_default_level),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_pull_enable),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_pull_select),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_get_input_level),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_set_interrupt_mask),
    DEFINE_MODULE_SYMBOL(pi4ioe5v6408_get_interrupt_level),
    MODULE_SYMBOL_TERMINATOR
};
