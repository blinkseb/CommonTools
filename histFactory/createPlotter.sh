#! /bin/bash

SKELETON=$1
CONFIG=$2
OUTPUT=$3

if [ -d "$OUTPUT" ]; then
    echo "Error: output folder $OUTPUT already exist"
    exit 1
fi

if [ ! -e "$CONFIG" ]; then
    echo "Error: python configuration file does not exist"
    exit 1
fi

# Create output folder
mkdir -p "$OUTPUT/build/external"

# Copy files
cp -r ../cmake "$OUTPUT"
cp ../templates/CMakeLists.txt "$OUTPUT/"
cp ../templates/generateHeader.sh "$OUTPUT/"

cp -r "external/src" "$OUTPUT/build/external/"
cp -r "external/lib" "$OUTPUT/build/external/"
cp -r "external/include" "$OUTPUT/build/external/"

# Generate C++ code
./createPlotter.exe -i "$SKELETON" -o "$OUTPUT" $CONFIG

pushd "$OUTPUT/build" &> /dev/null

cmake .. && make -j4

popd &> /dev/null

echo "Plotter is ready in '${OUTPUT}/build'"
