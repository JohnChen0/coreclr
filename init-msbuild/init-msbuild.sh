#!/usr/bin/env bash

__scriptpath=$(cd "$(dirname "$0")"; pwd -P)
__DOTNET_CMD=$1
__TOOLS_DIR=$2

__BUILDERRORLEVEL=0

if [ ! -e "$__DOTNET_CMD" ]; then
   echo "ERROR: Cannot find dotnet.exe at path '$__DOTNET_CMD'. Please pass in the path to dotnet.exe as the 1st parameter."
   exit 1
fi

if [ ! -e "$__TOOLS_DIR" ]; then
   echo "ERROR: Cannot find Tools directory at path '$__TOOLS_DIR'. Please pass in the Tools directory as the 2nd parameter."
   exit 1
fi

cd $__scriptpath
$__DOTNET_CMD restore --source $__scriptpath/NuGet/ --source https://www.myget.org/F/dotnet-core/ --source https://www.myget.org/F/dotnet-buildtools/ --source https://www.nuget.org/api/v2/
$__DOTNET_CMD publish -f dnxcore50 -o $__TOOLS_DIR

exit $__BUILDERRORLEVEL
