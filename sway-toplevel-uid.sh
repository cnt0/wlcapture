#!/bin/sh -e

swaymsg -r -t get_tree | jq -r '.. | select(.app_id?) | "\(.app_id):\t\(.name)\t\(.foreign_toplevel_identifier)"' |
	fzf --delimiter=$'\t' --with-nth=1,2 | cut -f3
