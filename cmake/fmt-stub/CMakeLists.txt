# Intentionally empty.
# score (through libossia) already provides fmt / fmt::fmt as global targets.
# We pre-declare FetchContent "fmt" pointing here so that flucoma-core's own
# FetchContent_Declare(fmt ...) is ignored and no duplicate target is created.
