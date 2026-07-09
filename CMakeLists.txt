cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

if(NOT TARGET score_lib_base)
  include(ScoreExternalAddon)
endif()

if(NOT TARGET score_plugin_avnd)
  return()
endif()

project(score_addon_flucoma LANGUAGES CXX)

avnd_score_plugin_init(
  BASE_TARGET score_addon_flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/Flucoma.hpp
  TARGET flucoma
  MAIN_CLASS Flucoma
  NAMESPACE Flucoma
)

avnd_score_plugin_finalize(
  BASE_TARGET score_addon_flucoma
  PLUGIN_VERSION 1
  PLUGIN_UUID "04545931-4450-4466-AA77-56F174F79F01"
)
