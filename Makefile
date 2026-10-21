# Student 1
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Isrc
LDFLAGS = -pthread

SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/smartprint

SOURCES = $(wildcard $(SRC_DIR)/*.c)
HEADERS = $(wildcard $(SRC_DIR)/*.h)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

SWF_LOG = datasets/KTH-SP2-1996-2.1-cln.swf.gz
REAL_WORKLOAD = workloads/real_kth_sp2.csv

.PHONY: all clean run demo contention real dashboard

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Every object depends on every header, so a struct change rebuilds all.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(TARGET) workloads/test_b_convoy.csv

demo: all
	rm -f queue_log.csv
	./$(TARGET) workloads/test_g_multilevel.csv --demo

# Bursts where Dean, Faculty and two Student submitters all have a job due
# at the same tick: threads block on the mutex and queues compete for the printer.
contention: all
	rm -f queue_log.csv
	./$(TARGET) workloads/test_h_contention.csv --demo

# Real-world input: 200 jobs from the KTH SP2 log (Parallel Workloads Archive),
# converted by tools/swf_to_workload.py. Regenerates the CSV if it is missing.
$(REAL_WORKLOAD): $(SWF_LOG) tools/swf_to_workload.py
	python3 -I tools/swf_to_workload.py $(SWF_LOG) $@ --start 0 --count 200 --load 0.9

real: all $(REAL_WORKLOAD)
	./$(TARGET) $(REAL_WORKLOAD)

# Local dashboard: run every workload, save each run's output, and turn the
# results into dashboard/data.js. Then open dashboard/index.html in a browser.
dashboard: all $(REAL_WORKLOAD)
	mkdir -p dashboard/runs
	rm -f dashboard/runs/run_*.txt
	for f in workloads/*.csv; do ./$(TARGET) $$f > dashboard/runs/run_$$(basename $$f .csv).txt; done
	python3 -I tools/build_dashboard_data.py dashboard/runs dashboard
	@echo "Open dashboard/index.html in a browser (from WSL: explorer.exe \"\$$(wslpath -w dashboard/index.html)\")"

clean:
	rm -rf $(BUILD_DIR)
