__DOTNET_CMD=$1
__MSBUILDRUNTIME_DIR=$2
__TOOLSDIR=$3
__BUILDTOOLS_PACKAGE_DIR=$(cd "$(dirname "$0")"; pwd -P)
__MSBUILD_CONTENT_JSON="{\"dependencies\": {\"Microsoft.Portable.Targets\": \"0.1.1-dev\"},\"frameworks\": {\"dnxcore50\": {},\"net46\": {}}}"

OSName=$(uname -s)
case $OSName in
  Darwin)
    __PUBLISH_RID=osx.10.10-x64
    ;;

  Linux)
    __PUBLISH_RID=ubuntu.14.04-x64
    ;;

  *)
    echo "Unsupported OS $OSName detected. Downloading ubuntu-x64 tools"
    __PUBLISH_RID=ubuntu.14.04-x64
    ;;
esac

if [ ! -e "$__DOTNET_CMD" ]; then
  echo "ERROR: Cannot find the dotnet.exe at path '$__DOTNET_CMD'. Please pass in the path to dotnet.exe as the 1st parameter"
  exit 1
fi

if [ -z "$__MSBUILDRUNTIME_DIR" ]; then
  echo "ERROR: MSBuild Runtime Directory is required for the restore. Please pass in the path where MSBuild will be restored as the 2nd parameter."
  exit 1
fi

if [ ! -d "$__TOOLSDIR" ]; then
  echo "ERROR: Cannot find the buildtools location at '$__TOOLSDIR'. Please pass in the path to buildtools as the 3rd parameter."
  exit 1
fi

# Restoring most of the runtime
cd "$__BUILDTOOLS_PACKAGE_DIR/msbuild-runtime/"
"$__DOTNET_CMD" restore "$__BUILDTOOLS_PACKAGE_DIR/msbuild-runtime/project.json" --source $__TOOLSDIR/../init-msbuild/NuGet/ --source https://www.myget.org/F/roslyn-nightly/ --source https://www.myget.org/F/dotnet-core/ --source https://www.myget.org/F/dotnet-buildtools/ --source https://www.nuget.org/api/v2/
"$__DOTNET_CMD" publish -f dnxcore50 -r ${__PUBLISH_RID} -o "$__MSBUILDRUNTIME_DIR"
chmod a+x "$__MSBUILDRUNTIME_DIR/corerun"

# Copy buildtools over to msbuild dir since msbuild currently can't load an external task library.
cp -n $__TOOLSDIR/* $__MSBUILDRUNTIME_DIR

exit 0
