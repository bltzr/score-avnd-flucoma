cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

if(NOT TARGET score_lib_base)
  include(ScoreExternalAddon)
endif()

if(NOT TARGET score_plugin_avnd)
  return()
endif()

project(score_addon_flucoma LANGUAGES CXX)

block()
  set(BUILD_SHARED_LIBS OFF)

  # score already provides fmt::fmt (see libossia cmake/deps/fmt.cmake); make
  # flucoma-core's FetchContent_Declare(fmt) a no-op by declaring it first.
  if(TARGET fmt::fmt)
    include(FetchContent)
    FetchContent_Declare(fmt
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fmt-stub"
    )
  endif()

  # flucoma-core is built against score's Eigen (baked into the shared avnd
  # PCH), which is newer than flucoma's pinned 3.4.0. Apply the compatibility
  # patch if it isn't already in the submodule working tree.
  execute_process(
    COMMAND git apply --check
            "${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-flucoma-core-eigen-master-compat.patch"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/flucoma-core"
    RESULT_VARIABLE _flucoma_patch_applicable
    ERROR_QUIET
  )
  if(_flucoma_patch_applicable EQUAL 0)
    execute_process(
      COMMAND git apply
              "${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-flucoma-core-eigen-master-compat.patch"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/flucoma-core"
    )
    message(STATUS "score-avnd-flucoma: applied Eigen compatibility patch to flucoma-core")
  endif()

  add_subdirectory(3rdparty/flucoma-core EXCLUDE_FROM_ALL)
endblock()

avnd_score_plugin_init(
  BASE_TARGET score_addon_flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/AmpSlice.hpp
    Flucoma/FluidBridge.hpp
  TARGET fluid_ampslice
  MAIN_CLASS AmpSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/Pitch.hpp
  TARGET fluid_pitch
  MAIN_CLASS Pitch
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/MFCC.hpp
  TARGET fluid_mfcc
  MAIN_CLASS MFCC
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/Loudness.hpp
  TARGET fluid_loudness
  MAIN_CLASS Loudness
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/SpectralShape.hpp
  TARGET fluid_spectralshape
  MAIN_CLASS SpectralShape
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/AmpFeature.hpp
  TARGET fluid_ampfeature
  MAIN_CLASS AmpFeature
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/OnsetFeature.hpp
  TARGET fluid_onsetfeature
  MAIN_CLASS OnsetFeature
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/NoveltyFeature.hpp
  TARGET fluid_noveltyfeature
  MAIN_CLASS NoveltyFeature
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/MelBands.hpp
  TARGET fluid_melbands
  MAIN_CLASS MelBands
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/Chroma.hpp
  TARGET fluid_chroma
  MAIN_CLASS Chroma
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/SineFeature.hpp
  TARGET fluid_sinefeature
  MAIN_CLASS SineFeature
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/RunningStats.hpp
  TARGET fluid_runningstats
  MAIN_CLASS RunningStats
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/HPSS.hpp
  TARGET fluid_hpss
  MAIN_CLASS HPSS
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/Transient.hpp
  TARGET fluid_transient
  MAIN_CLASS Transient
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/Sines.hpp
  TARGET fluid_sines
  MAIN_CLASS Sines
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/Gain.hpp
  TARGET fluid_gain
  MAIN_CLASS Gain
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BaseSTFT.hpp
  TARGET fluid_basestft
  MAIN_CLASS BaseSTFT
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/AudioTransport.hpp
  TARGET fluid_audiotransport
  MAIN_CLASS AudioTransport
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/OnsetSlice.hpp
  TARGET fluid_onsetslice
  MAIN_CLASS OnsetSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/NoveltySlice.hpp
  TARGET fluid_noveltyslice
  MAIN_CLASS NoveltySlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/TransientSlice.hpp
  TARGET fluid_transientslice
  MAIN_CLASS TransientSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/AmpGate.hpp
  TARGET fluid_ampgate
  MAIN_CLASS AmpGate
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES
    Flucoma/BufStats.hpp
    Flucoma/FluidBuffers.hpp
  TARGET fluid_bufstats
  MAIN_CLASS BufStats
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufNMF.hpp
  TARGET fluid_bufnmf
  MAIN_CLASS BufNMF
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/NMFFilter.hpp
  TARGET fluid_nmffilter
  MAIN_CLASS NMFFilter
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/NMFMatch.hpp
  TARGET fluid_nmfmatch
  MAIN_CLASS NMFMatch
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/NMFMorph.hpp
  TARGET fluid_nmfmorph
  MAIN_CLASS NMFMorph
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufCompose.hpp
  TARGET fluid_bufcompose
  MAIN_CLASS BufCompose
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufFlatten.hpp
  TARGET fluid_bufflatten
  MAIN_CLASS BufFlatten
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufScale.hpp
  TARGET fluid_bufscale
  MAIN_CLASS BufScale
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufSelect.hpp
  TARGET fluid_bufselect
  MAIN_CLASS BufSelect
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufSelectEvery.hpp
  TARGET fluid_bufselectevery
  MAIN_CLASS BufSelectEvery
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufThresh.hpp
  TARGET fluid_bufthresh
  MAIN_CLASS BufThresh
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufSTFT.hpp
  TARGET fluid_bufstft
  MAIN_CLASS BufSTFT
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufPitch.hpp
  TARGET fluid_bufpitch
  MAIN_CLASS BufPitch
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufLoudness.hpp
  TARGET fluid_bufloudness
  MAIN_CLASS BufLoudness
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufMFCC.hpp
  TARGET fluid_bufmfcc
  MAIN_CLASS BufMFCC
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufMelBands.hpp
  TARGET fluid_bufmelbands
  MAIN_CLASS BufMelBands
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufSpectralShape.hpp
  TARGET fluid_bufspectralshape
  MAIN_CLASS BufSpectralShape
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufChroma.hpp
  TARGET fluid_bufchroma
  MAIN_CLASS BufChroma
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufOnsetSlice.hpp
  TARGET fluid_bufonsetslice
  MAIN_CLASS BufOnsetSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufNoveltySlice.hpp
  TARGET fluid_bufnoveltyslice
  MAIN_CLASS BufNoveltySlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufAmpSlice.hpp
  TARGET fluid_bufampslice
  MAIN_CLASS BufAmpSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufTransientSlice.hpp
  TARGET fluid_buftransientslice
  MAIN_CLASS BufTransientSlice
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/BufAmpGate.hpp
  TARGET fluid_bufampgate
  MAIN_CLASS BufAmpGate
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/CorpusBuilder.hpp
  TARGET fluid_corpusbuilder
  MAIN_CLASS CorpusBuilder
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/KDTreeFit.hpp
  TARGET fluid_kdtreefit
  MAIN_CLASS KDTreeFit
  NAMESPACE Flucoma
)

avnd_score_plugin_add(
  BASE_TARGET score_addon_flucoma
  SOURCES Flucoma/KDTreeQuery.hpp
  TARGET fluid_kdtreequery
  MAIN_CLASS KDTreeQuery
  NAMESPACE Flucoma
)

avnd_score_plugin_finalize(
  BASE_TARGET score_addon_flucoma
  PLUGIN_VERSION 1
  PLUGIN_UUID "04545931-4450-4466-AA77-56F174F79F01"
)

target_link_libraries(score_addon_flucoma PRIVATE FLUID_DECOMPOSITION)

