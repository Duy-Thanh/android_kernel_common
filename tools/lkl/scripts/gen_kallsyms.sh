#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This shell script generates an object file with symbols compatible with Linux
# kernel `kallsyms` implementation.
#
# Parameters:
#  ${1} - path to the target executable from which to extract the symbols
#  ${2} - path to the produced assembly file with `kallsyms` symbols
#  ${3} - path to the produced object file from generated assembly file ${2}
#  ${4} - path to the kernel build ouptput directory (i.e. objtree)
#  ${5} - path to the kernel source code directory (i.e. srctree)

set -e

kallsymopt=

if grep -xq CONFIG_KALLSYMS_ALL=y ${4}/.config
then
  kallsymopt="${kallsymopt} --all-symbols"
fi

if grep -xq CONFIG_KALLSYMS_ABSOLUTE_PERCPU=y ${4}/.config
then
  kallsymopt="${kallsymopt} --absolute-percpu"
fi

if grep -xq CONFIG_KALLSYMS_BASE_RELATIVE=y ${4}/.config
then
  kallsymopt="${kallsymopt} --base-relative"
fi


aflags="-D__ASSEMBLY__ -D__KERNEL__"
aflags+=" -Werror=unknown-warning-option -integrated-as"
aflags+=" -nostdinc -I${4}/include/ -I${5}/arch/lkl/include"
aflags+=" -I${4}/tools/lkl/include/lkl/"
aflags+=" -I${4}/tools/lkl/include/"
aflags+=" -include ${5}/include/linux/kconfig.h"

${5}/scripts/mksysmap ${1} ${1}.syms
${4}/scripts/kallsyms ${kallsymopt} ${1}.syms > ${2}
${CC} ${aflags} -c -o ${3} ${2}
