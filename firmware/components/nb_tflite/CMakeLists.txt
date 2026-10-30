# NB3-only third-party code for the wake word: TFLite Micro (declared in
# idf_component.yml) and the TFLite Micro audio frontend from
# pymicro-features (fetched at its tag by fetch_repos.py). pymicro-features
# is what microWakeWord computes its training features with, so building the
# same sources with the same flags gives the model the features it learned.
set(FEATURES_DIR "${CMAKE_CURRENT_LIST_DIR}/../../pymicro-features")
if(NOT EXISTS "${FEATURES_DIR}/tensorflow/lite/experimental/microfrontend/lib/frontend.cc")
    message(FATAL_ERROR "pymicro-features not found: run 'python fetch_repos.py' first")
endif()

# Same source list as pymicro-features' setup.py, minus the Python binding.
# kiss_fft_int16.cc includes the kissfft sources it needs.
set(FRONTEND_SOURCES
    kiss_fft_int16.cc fft.cc fft_util.cc filterbank.cc filterbank_util.cc
    frontend.cc frontend_util.cc log_lut.cc log_scale.cc log_scale_util.cc
    noise_reduction.cc noise_reduction_util.cc pcan_gain_control.cc
    pcan_gain_control_util.cc window.cc window_util.cc)
list(TRANSFORM FRONTEND_SOURCES
     PREPEND "${FEATURES_DIR}/tensorflow/lite/experimental/microfrontend/lib/")

idf_component_register(SRCS ${FRONTEND_SOURCES}
                       INCLUDE_DIRS "${FEATURES_DIR}"
                       PRIV_INCLUDE_DIRS "${FEATURES_DIR}/kissfft"
                       REQUIRES esp-tflite-micro)

# pymicro-features builds with FIXED_POINT=16; third-party code keeps its own
# warnings out of NB3's -Werror.
target_compile_options(${COMPONENT_LIB} PRIVATE -DFIXED_POINT=16 -w)
