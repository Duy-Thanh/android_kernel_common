#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

#echo $(pwd) > /tmp/log.txt
echo $@ > /tmp/log.txt
cat << "END" | $@ -x c - -o /dev/null >/dev/null 2>>/tmp/log.txt
#include <stdio.h>
int main(void)
{
	printf("");
	return 0;
}
END
