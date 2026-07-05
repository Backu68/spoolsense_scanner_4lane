#pragma once

#include "IPrinterStrategy.h"
#include <cstring>

/// Mock printer strategy for unit testing PrinterManager.
/// Test code sets state directly, then calls PrinterManager::poll().
class MockPrinterStrategy : public IPrinterStrategy {
public:
    void update() override { updateCalled_++; }

    bool hasActiveJob() const override { return hasJob_; }
    int getJobId() const override { return jobId_; }
    float getProgress() const override { return progress_; }
    float getTotalFilamentGrams() const override { return totalFilamentG_; }
    const char* getJobState() const override { return jobState_; }
    bool isConnected() const override { return connected_; }

    const char* getExpectedFilamentType() const override { return expectedFilamentType_; }
    float getExpectedNozzleTemp() const override { return expectedNozzleTemp_; }
    float getExpectedBedTemp() const override { return expectedBedTemp_; }

    float fetchDeferredFilament(int expectedJobId) override {
        deferredCalled_++;
        if (expectedJobId == deferredJobId_ && deferredFilamentG_ > 0.0f) {
            return deferredFilamentG_;
        }
        return 0.0f;
    }

    // --- Test controls ---

    void setIdle() {
        connected_ = true;
        hasJob_ = false;
        jobId_ = -1;
        progress_ = 0.0f;
        totalFilamentG_ = 0.0f;
        jobState_[0] = '\0';
    }

    void setPrinting(int jobId, float filamentG, float progress = 0.0f) {
        connected_ = true;
        hasJob_ = true;
        jobId_ = jobId;
        totalFilamentG_ = filamentG;
        progress_ = progress;
        strncpy(jobState_, "PRINTING", sizeof(jobState_));
    }

    void setFinished(float progress = 100.0f) {
        strncpy(jobState_, "FINISHED", sizeof(jobState_));
        progress_ = progress;
    }

    void setStopped(float progress) {
        strncpy(jobState_, "STOPPED", sizeof(jobState_));
        progress_ = progress;
    }

    void setDisconnected() {
        connected_ = false;
    }

    void clearJob() {
        hasJob_ = false;
    }

    void setExpectedFilament(const char* type, float nozzleTemp = 0, float bedTemp = 0) {
        strncpy(expectedFilamentType_, type, sizeof(expectedFilamentType_) - 1);
        expectedNozzleTemp_ = nozzleTemp;
        expectedBedTemp_ = bedTemp;
    }

    void setDeferredFilament(int jobId, float grams) {
        deferredJobId_ = jobId;
        deferredFilamentG_ = grams;
    }

    void setPerToolData(int count, const float* grams) {
        toolCount_ = (count > MAX_TOOLS) ? MAX_TOOLS : count;
        for (int i = 0; i < toolCount_; i++) {
            filamentPerTool_[i] = grams[i];
        }
    }

    int getToolCount() const override { return toolCount_; }
    float getFilamentForTool(int idx) const override {
        return (idx >= 0 && idx < toolCount_) ? filamentPerTool_[idx] : 0.0f;
    }

    int updateCalled_ = 0;
    int deferredCalled_ = 0;

    // All fields public for test access
    bool connected_ = false;
    bool hasJob_ = false;
    int jobId_ = -1;
    float progress_ = 0.0f;
    float totalFilamentG_ = 0.0f;
    char jobState_[16] = {0};
    char expectedFilamentType_[32] = {0};
    float expectedNozzleTemp_ = 0.0f;
    float expectedBedTemp_ = 0.0f;
    int deferredJobId_ = -1;
    float deferredFilamentG_ = 0.0f;
    int toolCount_ = 0;
    float filamentPerTool_[MAX_TOOLS] = {0};
};
