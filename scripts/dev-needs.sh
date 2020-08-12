#!/bin/sh
# Copyright (c) 2020, Google LLC. All rights reserved.
# Author: Saravana Kannan <saravanak@google.com>

function numbered() {
	local i=0
	for s in $@
	do
		echo " ${i}"$'\t'$s
		i=$((i+1))
	done
}

function add_suppliers() {
	CON=$1

	# If this is not a device with a driver, we don't care about its
	# suppliers.
	if [ ! -e $CON/driver ]
	then
		return 1
	fi

	SUPPLIER_LINKS=$(ls -1d $CON/supplier:* 2>/dev/null)
	for SL in $SUPPLIER_LINKS;
	do
		SYNC_STATE=$(cat $SL/sync_state_only)

		# sync_state_only links are proxy dependencies.
		# They can also have cycles. So, don't follow them.
		if [ "$SYNC_STATE" != '0' ]
		then
			continue
		fi

		SUPPLIER=$(realpath $SL/supplier)

		CONSUMERS+=($SUPPLIER)
	done
	return 0
}

case $1 in
	-c)
		function detail() {
			f=$1/of_node/compatible
			if [ -e $f ]
			then
				echo -n $(cat $f)
			else
				echo -n $1
			fi
		}
		shift
		;;
	-m)
		function detail() {
			f=$1/driver/module
			if [ -e $f ]
			then
				echo -n $(basename $(realpath $f))
			else
				echo -n $1
			fi
		}
		shift
		;;
	-d)
		function detail() {
			f=$1/driver
			if [ -e $f ]
			then
				echo -n $(basename $(realpath $f))
			else
				echo -n $1
			fi
		}
		shift
		;;
	-f)
		function detail() {
			f=$1/firmware_node
			if [ -e $f ]
			then
				echo -n $(realpath $f)
			else
				echo -n $1
			fi
		}
		shift
		;;
	*)
		function detail() { echo -n $1; }
		;;
esac

CONSUMERS=($@)
OUT_LIST=()

# Do a breadth first, non-recursive tracking of suppliers. The parent is also
# considered a "supplier" as a device can't probe without its parent.
i=0
while [ $i -lt ${#CONSUMERS[@]} ]
do
	CONSUMER=${CONSUMERS[$i]}
	i=$(($i+1))

	PARENT=$(realpath $CONSUMER/..)

	if [ "${PARENT}" != "/sys/devices" ]
	then
		CONSUMERS+=(${PARENT})
	fi

	# Add suppliers to CONSUMERS list and output the consumer details.
	if add_suppliers ${CONSUMER}
	then
		OUT_LIST+=($(detail ${CONSUMER}))
	fi
done

numbered ${OUT_LIST[@]} | sort -suk2 | sort -n | cut -f2-
