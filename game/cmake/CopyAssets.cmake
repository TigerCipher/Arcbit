# Invoked at build time via cmake -P.
# file(COPY) checks source timestamps before copying — unchanged files are
# skipped, new files are picked up, no CMake reconfigure required.
file(COPY "${ASSETS_SRC}/" DESTINATION "${ASSETS_DST}")
