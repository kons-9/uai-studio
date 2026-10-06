#include "driver/driver_ownership.hpp"

#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace {

struct MutexMock {
    int created = 0;
    int create_result = 1;
    int lock_result = E_OK;
    int last_attr = -1;
    TMO last_timeout = 0;
    std::vector<ID> locks;
    std::vector<ID> unlocks;
    void Reset() { *this = {}; }
} g_mutex;

} // namespace

ID tk_cre_mtx(const T_CMTX *config)
{
    g_mutex.last_attr = config->mtxatr;
    ++g_mutex.created;
    return g_mutex.create_result;
}
ER tk_loc_mtx(ID mutex, TMO timeout)
{
    g_mutex.last_timeout = timeout;
    if (g_mutex.lock_result != E_OK) return g_mutex.lock_result;
    g_mutex.locks.push_back(mutex);
    return E_OK;
}
ER tk_unl_mtx(ID mutex)
{
    g_mutex.unlocks.push_back(mutex);
    return E_OK;
}

namespace uai::ai::driver {
namespace {

class ResourceOwnershipTest : public ::testing::Test {
protected:
    void SetUp() override { g_mutex.Reset(); }
};

TEST_F(ResourceOwnershipTest, InitializeCreatesInheritMutexOnce)
{
    ResourceManagement management;
    ResourceManagement::Writer writer;
    EXPECT_EQ(management.Acquire(&writer).Code(),
              common::ErrorCode::kNotInitialized);
    EXPECT_FALSE(writer.Valid());
    EXPECT_EQ(management.Validate(writer).Code(),
              common::ErrorCode::kNotInitialized);

    ASSERT_TRUE(management.Initialize().Ok());
    EXPECT_EQ(g_mutex.created, 1);
    EXPECT_EQ(g_mutex.last_attr, TA_INHERIT);
    EXPECT_EQ(management.Initialize().Code(),
              common::ErrorCode::kAlreadyInitialized);
    EXPECT_EQ(g_mutex.created, 1);

    ResourceManagement failing;
    g_mutex.create_result = -1;
    EXPECT_EQ(failing.Initialize().Code(), common::ErrorCode::kOwnership);
    EXPECT_EQ(failing.Acquire(&writer).Code(),
              common::ErrorCode::kNotInitialized);
}

TEST_F(ResourceOwnershipTest, AcquireMapsTimeoutAndReleasesExactlyOnce)
{
    ResourceManagement management;
    ASSERT_TRUE(management.Initialize().Ok());
    EXPECT_EQ(management.Acquire(nullptr).Code(),
              common::ErrorCode::kInvalidArgument);

    g_mutex.lock_result = E_TMOUT;
    ResourceManagement::Writer writer;
    EXPECT_EQ(management.Acquire(&writer, 5).Code(),
              common::ErrorCode::kTimeout);
    EXPECT_EQ(g_mutex.last_timeout, 5);
    EXPECT_FALSE(writer.Valid());
    g_mutex.lock_result = -99;
    EXPECT_EQ(management.Acquire(&writer).Code(),
              common::ErrorCode::kOwnership);
    EXPECT_EQ(g_mutex.last_timeout, TMO_FEVR);

    g_mutex.lock_result = E_OK;
    {
        ResourceManagement::Writer held;
        ASSERT_TRUE(management.Acquire(&held).Ok());
        EXPECT_TRUE(held.Valid());
        EXPECT_TRUE(management.Validate(held).Ok());
        EXPECT_EQ(g_mutex.locks.size(), 1U);

        ResourceManagement::Writer moved(std::move(held));
        EXPECT_FALSE(held.Valid());
        EXPECT_TRUE(moved.Valid());
        EXPECT_EQ(management.Validate(held).Code(),
                  common::ErrorCode::kOwnership);
        EXPECT_TRUE(management.Validate(moved).Ok());

        ResourceManagement::Writer &self = moved;
        moved = std::move(self);
        EXPECT_TRUE(moved.Valid());
        EXPECT_TRUE(g_mutex.unlocks.empty());
    }
    EXPECT_EQ(g_mutex.unlocks.size(), 1U);
    EXPECT_EQ(g_mutex.unlocks[0], g_mutex.locks[0]);
}

TEST_F(ResourceOwnershipTest, MoveAssignReleasesPreviousAndRejectsForeignWriter)
{
    ResourceManagement first;
    ResourceManagement second;
    g_mutex.create_result = 11;
    ASSERT_TRUE(first.Initialize().Ok());
    g_mutex.create_result = 22;
    ASSERT_TRUE(second.Initialize().Ok());

    ResourceManagement::Writer a, b;
    ASSERT_TRUE(first.Acquire(&a).Ok());
    ASSERT_TRUE(second.Acquire(&b).Ok());
    EXPECT_EQ(first.Validate(b).Code(), common::ErrorCode::kOwnership);
    EXPECT_EQ(second.Validate(a).Code(), common::ErrorCode::kOwnership);

    a = std::move(b);
    EXPECT_EQ(g_mutex.unlocks, (std::vector<ID>{11}));
    EXPECT_FALSE(b.Valid());
    EXPECT_TRUE(second.Validate(a).Ok());

    ResourceManagement::Writer replaced;
    ASSERT_TRUE(first.Acquire(&replaced).Ok());
    ASSERT_TRUE(first.Acquire(&replaced).Ok());
    EXPECT_EQ(g_mutex.unlocks, (std::vector<ID>{11, 11}));
    EXPECT_EQ(g_mutex.locks.size(), 4U);
}

} // namespace
} // namespace uai::ai::driver
