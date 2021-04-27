#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Generates a list of Control-Flow Integrity (CFI) jump table symbols
# for kallsyms.
#
# Copyright (C) 2021 Google LLC

use strict;
use warnings;

## parameters
my $file = shift(@ARGV) || die "$0: usage $0 vmlinux";
## environment
my $readelf = $ENV{'READELF'} || die "$0: ERROR: READELF not set?";
my $objdump = $ENV{'OBJDUMP'} || die "$0: ERROR: OBJDUMP not set?";
my $nm = $ENV{'NM'} || die "$0: ERROR: OBJDUMP not set?";
## jump table addresses
my $cfi_jt = {};

## finds __cfi_jt_ symbols from the binary to locate the start and end of the
## jump table
sub find_cfi_jt {
	open(my $fh, "\"$readelf\" --symbols \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$readelf\": $!";

	while (<$fh>) {
		if (!($_ =~ /__cfi_jt_/)) {
			next;
		}

		chomp;

		my ($addr, $name) = $_ =~ /\:.*([a-f0-9]{16}).*\s__cfi_jt_(.*)/;
		if (defined($addr) && defined($name)) {
			$cfi_jt->{$name} = $addr;
		}
	}

	close($fh);
}

## walks through the jump table looking for branches and prints out a jump
## table symbol for each branch if one is missing
sub print_missing_symbols {
	my $start = $cfi_jt->{"start"}
		or die "$0: ERROR: __cfi_jt_start symbol missing";
	my $end   = $cfi_jt->{"end"}
		or die "$0: ERROR: __cfi_jt_end symbol missing";

	open(my $fh, "\"$objdump\" -d --start-address=0x$start " .
		     "--stop-address=0x$end \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$objdump\": $!";

	my $last_symbol;
	my $last_hint_addr;

	while (<$fh>) {
		chomp;

		# keep track of last found symbols
		my ($symbol_addr, $symbol) = $_ =~ /^([a-f0-9]{16})\s<([^>]+)>\:/;

		if (defined($symbol_addr) && defined($symbol)) {
			$last_symbol = $symbol;
			next;
		}

		# keep track of BTI hint addresses
		my ($hint) = $_ =~ /^([a-f0-9]{16})\:.*hint\s+#/;

		if (defined($hint)) {
			$last_hint_addr = $hint;
			next;
		}

		# branch to the beginning of a function
		my ($addr, $instr, $target) = $_ =~
			/^([a-f0-9]{16})\:.*(b|jmpq?)\s+0x[a-f0-9]{16}\s+<([^>\+]+)>/;

		if (!defined($addr) || !defined($target)) {
			next;
		}

		# ignore functions with a canonical jump table
		if ($target =~ /\.cfi$/) {
			next;
		}

		# use the hint address if available
		if (defined($last_hint_addr)) {
			$addr = $last_hint_addr;
			$last_hint_addr = undef;
		}

		# expected jump table symbol
		my $cfi_jt_symbol = $target . ".cfi_jt";
		$cfi_jt->{$addr} = $cfi_jt_symbol;

		if (defined($last_symbol) && $last_symbol eq $cfi_jt_symbol) {
			next; # already exists
		}

		# print out the symbol
		print "$addr t $cfi_jt_symbol\n"
	}

	close($fh);
}

## prints out the remaining symbols from nm -n, filtering out the unnecessary
## __typeid__ symbols aliasing the jump table symbols we added
sub print_kallsyms {
	open(my $fh, "\"$nm\" -n \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$nm\": $!";

	while (<$fh>) {
		chomp;

		my ($addr, $symbol) = $_ =~ /^([a-f0-9]{16})\s.\s(.*)$/;

		if (defined($addr) && defined($symbol)) {
			# drop duplicate __typeid__ symbols
			if ($symbol =~ /^__typeid__.*_global_addr$/ &&
				exists($cfi_jt->{$addr})) {
				next;
			}
		}

		print "$_\n";
	}

	close($fh);
}

find_cfi_jt();
print_missing_symbols();
print_kallsyms();
