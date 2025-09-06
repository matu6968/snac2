#!/bin/sh
#
# docker_build_snac.sh : build a SNAC container image
# 	and optionally send it to a container registry.
#
# Set variables e.g. if you want version to be different from 'latest':
# 	img_version=2.82 ./docker_build_snac.sh

src_dir=${src_dir:-"$HOME/src/snac2"}
img_name=${img_name:-'snac'}
img_version=${img_version:-'latest'}
#registry=${registry:-'codeberg.org'}
#reg_user=${reg_user:-'daltux'}

if [ -z "$tag" ] ; then
	if [ -n "$reg_user" ] && [ -z "$registry" ] ; then
		echo "Missing container registry name. Set variable \"registry\"." >&2
		exit 10
	fi

	if [ -z "$registry" ] ; then
		tag="$img_name:$img_version"
	elif [ -z "$reg_user" ] ; then
		echo "Container registry user unknown. Set variable \"reg_user\"." >&2
		exit 20
	else
		tag="$registry/$reg_user/$img_name:$img_version"
	fi
fi

if [ -d "$src_dir" ] ; then
	echo "Entering directory \"$src_dir\"..."
	cd "$src_dir" || exit $?
	docker build --no-cache -f Dockerfile -t "$tag" . || exit $?
else
	echo "Invalid directory \"$src_dir\"" >&2
	exit 30
fi

if [ -n "$registry" ] ; then
	#docker login "$registry" || $?
	docker image push "$tag"
fi

