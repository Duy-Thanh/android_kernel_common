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

function help() {
	cat << EOF
dev-needs.sh [-c|-d|-m|-f] <list of device paths in /sys/devices>

The script takes as input a list of one or more directories under
/sys/devices and then lists the probe dependency chain (suppliers and
parents) of these devices. It does a breadth first search of the dependency
chain, so the last entry in the output is close to the root of the
dependency chain.

By default it lists the full path to the devices under /sys/devices.

It also takes an optional modifier flag as the first parameter to change
what information is listed in the output:
-c      lists the compatible string of the dependencies
-d      lists the driver name of the dependencies
-m      lists the module name of the dependnecies
-f      list the firmware node path of the dependencies

EOF
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
			if [ ! -e $f ]
			then
				f=$1/of_node
			fi

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

if [ $# -eq 0 ]
then
	help
	exit 1
fi


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

exit 0
