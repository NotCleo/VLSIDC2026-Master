# --- Toolchain Configuration ---
# Updated to match your system's compiler: riscv64-linux-gnu-gcc
CC = riscv64-linux-gnu-gcc
CXX = riscv64-linux-gnu-g++
TARGET = sorting_test

# *** JPEG LIBRARY PATH (Updated to your specific location) ***
JPEG_PATH = /home/joelphilip/Documents/VLSID26/VectorBlox-SDK/example/soc-c/jpeg-9e/riscv-install

# Kit selection
kit = icicle
ifeq ($(kit),icicle)
    C_FLAGS+= -DUSE_INTERRUPTS=0 -DMSS_DDR
else
    C_FLAGS+= -DUSE_INTERRUPTS=1
endif

# --- Source Files ---

# 1. VBX Driver & Post-Processing
C_SRCS = pdma/pdma_helpers.c
C_SRCS += ../postprocess/image.c
C_SRCS += ../postprocess/libfixmath/fix16.c ../postprocess/libfixmath/fix16_exp.c ../postprocess/libfixmath/fix16_sqrt.c ../postprocess/libfixmath/fix16_str.c
C_SRCS += ../postprocess/libfixmath/fix16_trig.c ../postprocess/libfixmath/fract32.c ../postprocess/libfixmath/uint32.c
C_SRCS += ../postprocess/postprocess.c ../postprocess/postprocess_scrfd.c ../postprocess/postprocess_ssd.c ../postprocess/postprocess_retinaface.c ../postprocess/postprocess_license_plate.c ../postprocess/postprocess_pose.c
C_SRCS += ../../drivers/vectorblox/vbx_cnn_api.c ../../drivers/vectorblox/vbx_cnn_model.c

# 2. Application Files
C_SRCS += main-test.c uart.c ultrasonic.c camera.c

# 3. C++ Classifier
CXX_SRCS = classifier.cpp

# --- Object Generation Logic ---
C_OBJS = $(addsuffix .o,$(addprefix obj/,$(abspath $(C_SRCS))))
CXX_OBJS = $(addsuffix .o,$(addprefix obj/,$(abspath $(CXX_SRCS))))

# --- Compiler Flags ---
# Added -I$(JPEG_PATH)/include so it finds jpeglib.h
C_FLAGS += -Wall -O3 -I. -I$(JPEG_PATH)/include -I$(abspath ../../drivers/vectorblox/) -I$(abspath ../postprocess/libfixmath/) -Ipdma/ -I$(abspath ../postprocess/) -MD -DVBX_SOC_DRIVER -DPDMA

# --- Build Rules ---

all: $(TARGET)

# Compile C++ files
$(CXX_OBJS): obj/%.o: %
	mkdir -p $(dir $@)
	$(CXX) $(C_FLAGS) -std=c++11 -c $< -o $@

# Compile C files
$(C_OBJS): obj/%.o: %
	mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) -c $< -o $@

# Link everything together
# Added -L$(JPEG_PATH)/lib so it finds libjpeg.a
$(TARGET): $(CXX_OBJS) $(C_OBJS)
	$(CXX) -static -o $@ $^ -L$(JPEG_PATH)/lib -ljpeg -lm

.PHONY: clean
clean:
	rm -rf $(TARGET) obj