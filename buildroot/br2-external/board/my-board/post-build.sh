#!/usr/bin/bash

# The path to the target rootfs is provided as the first argument to the
# script.
if [[ -d $1 ]]; then
	# Modify the /etc/buildtime file created by the overlay.
	date > $1/etc/buildtime
fi

