rm -rf /tmp/spatialroot-alpha-stage
cmake --install build --prefix /tmp/spatialroot-alpha-stage --component SpatialRootRuntime

cd /tmp/spatialroot-alpha-stage
ditto -c -k --keepParent "Spatial Root.app" "$HOME/Desktop/SpatialRoot-alpha-v1-macOS.zip"
shasum -a 256 "$HOME/Desktop/SpatialRoot-alpha-v1-macOS.zip"



#broken