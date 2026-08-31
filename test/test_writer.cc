/* test_writer.cc - Test zip writer functionality
   part of the minizip-ng project

   Copyright (C) Nathan Moinvaziri
     https://github.com/zlib-ng/minizip-ng

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/

#include "mz.h"
#include "mz_os.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include <gtest/gtest.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  if defined(__APPLE__) || defined(__linux__)
#    include <sys/xattr.h>
#  endif
#endif

static int32_t remove_test_dir(const char *path) {
#if defined(_WIN32)
    return _rmdir(path);
#else
    return rmdir(path);
#endif
}

class WriterPathTest : public testing::Test {
  protected:
    void SetUp() override {
        char path[128];
        snprintf(path, sizeof(path), "test-writer-%" PRIu64, mz_os_ms_time());
        test_dir = path;
        ASSERT_EQ(MZ_OK, mz_os_make_dir(test_dir.c_str()));

        input_path = test_dir + "/a-input.bin";
        excluded_path = test_dir + "/m-archive.zip";
        alias_path = test_dir + "/n-archive-alias.zip";
        second_alias_path = test_dir + "/o-second-archive-alias.zip";
        output_path = test_dir + "/z-output.zip";
        wildcard_path = test_dir + "/*";
    }

    void TearDown() override {
        char split_path[256];

        std::remove(input_path.c_str());
        std::remove(excluded_path.c_str());
        std::remove(alias_path.c_str());
        std::remove(second_alias_path.c_str());
        std::remove(output_path.c_str());
        for (int32_t disk = 1; disk < 100; disk += 1) {
            snprintf(split_path, sizeof(split_path), "%s/z-output.z%02" PRId32, test_dir.c_str(), disk);
            std::remove(split_path);
        }
        remove_test_dir(test_dir.c_str());
    }

    void CreateInputFiles() {
        uint32_t value = 1;

        {
            std::ofstream input(input_path, std::ios::binary);
            ASSERT_TRUE(input.is_open());
            for (int32_t i = 0; i < 131072; i += 1) {
                value = value * 1103515245 + 12345;
                input.put((char)(value >> 24));
            }
        }
        {
            std::ofstream excluded(excluded_path, std::ios::binary);
            ASSERT_TRUE(excluded.is_open());
            excluded << "existing archive";
        }
    }

    void VerifySingleInputEntry() {
        void *reader = mz_zip_reader_create();
        mz_zip_file *file_info = nullptr;
        int32_t err = MZ_OK;
        int32_t entry_count = 0;

        ASSERT_NE(nullptr, reader);
        ASSERT_EQ(MZ_OK, mz_zip_reader_open_file(reader, output_path.c_str()));

        err = mz_zip_reader_goto_first_entry(reader);
        while (err == MZ_OK) {
            ASSERT_EQ(MZ_OK, mz_zip_reader_entry_get_info(reader, &file_info));
            EXPECT_STREQ("a-input.bin", file_info->filename);
            entry_count += 1;
            err = mz_zip_reader_goto_next_entry(reader);
        }

        EXPECT_EQ(MZ_END_OF_LIST, err);
        EXPECT_EQ(1, entry_count);
        EXPECT_EQ(MZ_OK, mz_zip_reader_close(reader));
        mz_zip_reader_delete(&reader);
    }

    std::string test_dir;
    std::string input_path;
    std::string excluded_path;
    std::string alias_path;
    std::string second_alias_path;
    std::string output_path;
    std::string wildcard_path;
};

TEST_F(WriterPathTest, excludes_output_and_configured_source_path) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file(writer, output_path.c_str(), 0, 0));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    VerifySingleInputEntry();
}

TEST_F(WriterPathTest, excludes_split_output_paths) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file(writer, output_path.c_str(), 32768, 0));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    VerifySingleInputEntry();
}

TEST_F(WriterPathTest, exclusive_open_does_not_truncate_existing_file) {
    void *writer = nullptr;
    std::ifstream excluded;
    std::string contents;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    EXPECT_EQ(MZ_EXIST_ERROR, mz_zip_writer_open_file_exclusive(writer, excluded_path.c_str()));
    EXPECT_EQ(MZ_OPEN_ERROR, mz_zip_writer_open_file_exclusive(writer, (excluded_path + "/archive.zip").c_str()));
    mz_zip_writer_delete(&writer);

    excluded.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(excluded.is_open());
    contents.assign(std::istreambuf_iterator<char>(excluded), std::istreambuf_iterator<char>());
    EXPECT_EQ("existing archive", contents);
}

TEST_F(WriterPathTest, prepared_paths_do_not_include_output_created_after_scan) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_prepare_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file_exclusive(writer, output_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_prepared_paths(writer));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    VerifySingleInputEntry();
}

TEST_F(WriterPathTest, prepared_paths_are_released_when_writer_is_deleted) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    ASSERT_EQ(MZ_OK, mz_zip_writer_prepare_path(writer, input_path.c_str(), nullptr, 0, 1));
    mz_zip_writer_delete(&writer);
    EXPECT_EQ(nullptr, writer);
}

#if defined(_WIN32)
TEST_F(WriterPathTest, replace_follows_output_symlink_on_windows) {
    std::ifstream target;
    std::string contents;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    if (mz_os_make_symlink(alias_path.c_str(), "m-archive.zip") != MZ_OK)
        GTEST_SKIP() << "Creating symbolic links is not permitted";

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_os_is_symlink(alias_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", contents);
}

TEST_F(WriterPathTest, replace_preserves_hard_link_contents_on_windows) {
    std::ifstream target;
    std::ifstream alias;
    std::string target_contents;
    std::string alias_contents;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    if (!CreateHardLinkA(alias_path.c_str(), excluded_path.c_str(), NULL))
        GTEST_SKIP() << "Creating hard links is not supported by the test filesystem";

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), excluded_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    alias.open(alias_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    ASSERT_TRUE(alias.is_open());
    target_contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    alias_contents.assign(std::istreambuf_iterator<char>(alias), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", target_contents);
    EXPECT_EQ(target_contents, alias_contents);
}
#endif

#if !defined(_WIN32)
TEST_F(WriterPathTest, replace_follows_output_symlink) {
    std::ifstream target;
    std::string contents;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    ASSERT_EQ(0, symlink("m-archive.zip", alias_path.c_str()));

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_os_is_symlink(alias_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", contents);
}

TEST_F(WriterPathTest, replace_follows_dangling_output_symlink) {
    std::ifstream target;
    std::string contents;

    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    ASSERT_EQ(0, symlink("m-archive.zip", alias_path.c_str()));

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_os_is_symlink(alias_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", contents);
}

TEST_F(WriterPathTest, replace_follows_dangling_output_symlink_chain) {
    std::ifstream target;
    std::string contents;

    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    ASSERT_EQ(0, symlink("o-second-archive-alias.zip", alias_path.c_str()));
    ASSERT_EQ(0, symlink("m-archive.zip", second_alias_path.c_str()));

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_os_is_symlink(alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_os_is_symlink(second_alias_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", contents);
}

TEST_F(WriterPathTest, replace_resolved_does_not_follow_swapped_symlink) {
    char *replace_path = nullptr;
    std::ifstream target;
    std::string contents;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }

    ASSERT_EQ(MZ_OK, mz_os_get_replace_path(excluded_path.c_str(), &replace_path));
    ASSERT_NE(nullptr, replace_path);
    ASSERT_EQ(0, std::rename(excluded_path.c_str(), alias_path.c_str()));
    ASSERT_EQ(0, symlink("n-archive-alias.zip", excluded_path.c_str()));

    ASSERT_EQ(MZ_OK, mz_os_replace_resolved(output_path.c_str(), replace_path));
    free(replace_path);
    replace_path = nullptr;
    EXPECT_NE(MZ_OK, mz_os_is_symlink(excluded_path.c_str()));

    target.open(alias_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("existing archive", contents);

    target.close();
    target.open(excluded_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", contents);
}

TEST_F(WriterPathTest, replace_preserves_permissions) {
    struct stat target_stat;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    ASSERT_EQ(0, chmod(excluded_path.c_str(), 0604));

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), excluded_path.c_str()));
    ASSERT_EQ(0, stat(excluded_path.c_str(), &target_stat));
    EXPECT_EQ(0604, target_stat.st_mode & 0777);
}

#if defined(__APPLE__) || defined(__linux__)
TEST_F(WriterPathTest, replace_preserves_extended_attributes) {
    const char attribute_name[] = "user.minizip-review";
    const char attribute_value[] = "preserved";
    char value[32];
    int32_t set_result = 0;
    ssize_t value_size = 0;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }

#  if defined(__APPLE__)
    set_result = setxattr(excluded_path.c_str(), attribute_name, attribute_value, strlen(attribute_value), 0, 0);
#  else
    set_result = setxattr(excluded_path.c_str(), attribute_name, attribute_value, strlen(attribute_value), 0);
#  endif
    if (set_result != 0)
        GTEST_SKIP() << "Extended attributes are not supported by the test filesystem";

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), excluded_path.c_str()));
#  if defined(__APPLE__)
    value_size = getxattr(excluded_path.c_str(), attribute_name, value, sizeof(value), 0, 0);
#  else
    value_size = getxattr(excluded_path.c_str(), attribute_name, value, sizeof(value));
#  endif
    ASSERT_EQ((ssize_t)strlen(attribute_value), value_size);
    EXPECT_EQ(0, memcmp(attribute_value, value, (size_t)value_size));
}
#endif

#if defined(__linux__)
TEST_F(WriterPathTest, replace_removes_source_only_extended_attributes) {
    const char attribute_name[] = "user.minizip-source-only";
    const char attribute_value[] = "remove-me";
    char value[32];

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }

    if (setxattr(output_path.c_str(), attribute_name, attribute_value, strlen(attribute_value), 0) != 0)
        GTEST_SKIP() << "Extended attributes are not supported by the test filesystem";

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), excluded_path.c_str()));
    errno = 0;
    EXPECT_EQ(-1, getxattr(excluded_path.c_str(), attribute_name, value, sizeof(value)));
    EXPECT_EQ(ENODATA, errno);
}
#endif

TEST_F(WriterPathTest, replace_preserves_hard_link_contents) {
    struct stat target_stat;
    struct stat alias_stat;
    std::ifstream target;
    std::ifstream alias;
    std::string target_contents;
    std::string alias_contents;

    CreateInputFiles();
    {
        std::ofstream replacement(output_path, std::ios::binary);
        ASSERT_TRUE(replacement.is_open());
        replacement << "replacement archive";
    }
    ASSERT_EQ(0, link(excluded_path.c_str(), alias_path.c_str()));

    ASSERT_EQ(MZ_OK, mz_os_replace(output_path.c_str(), excluded_path.c_str()));
    EXPECT_NE(MZ_OK, mz_os_file_exists(output_path.c_str()));

    target.open(excluded_path, std::ios::binary);
    alias.open(alias_path, std::ios::binary);
    ASSERT_TRUE(target.is_open());
    ASSERT_TRUE(alias.is_open());
    target_contents.assign(std::istreambuf_iterator<char>(target), std::istreambuf_iterator<char>());
    alias_contents.assign(std::istreambuf_iterator<char>(alias), std::istreambuf_iterator<char>());
    EXPECT_EQ("replacement archive", target_contents);
    EXPECT_EQ(target_contents, alias_contents);

    ASSERT_EQ(0, stat(excluded_path.c_str(), &target_stat));
    ASSERT_EQ(0, stat(alias_path.c_str(), &alias_stat));
    EXPECT_EQ(target_stat.st_dev, alias_stat.st_dev);
    EXPECT_EQ(target_stat.st_ino, alias_stat.st_ino);
}

TEST_F(WriterPathTest, excludes_hard_link_to_output) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file(writer, output_path.c_str(), 0, 0));
    ASSERT_EQ(0, link(output_path.c_str(), alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    VerifySingleInputEntry();
}

TEST_F(WriterPathTest, excludes_followed_symlink_to_output) {
    void *writer = nullptr;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    mz_zip_writer_set_follow_links(writer, 1);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file(writer, output_path.c_str(), 0, 0));
    ASSERT_EQ(0, symlink("z-output.zip", alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    VerifySingleInputEntry();
}

TEST_F(WriterPathTest, stores_symlink_to_output) {
    void *reader = nullptr;
    void *writer = nullptr;
    mz_zip_file *file_info = nullptr;
    int32_t entry_count = 0;
    int32_t err = MZ_OK;
    bool found_input = false;
    bool found_link = false;

    CreateInputFiles();

    writer = mz_zip_writer_create();
    ASSERT_NE(nullptr, writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
    mz_zip_writer_set_store_links(writer, 1);
    ASSERT_EQ(MZ_OK, mz_zip_writer_set_exclude_path(writer, excluded_path.c_str()));
    ASSERT_EQ(MZ_OK, mz_zip_writer_open_file(writer, output_path.c_str(), 0, 0));
    ASSERT_EQ(0, symlink("z-output.zip", alias_path.c_str()));
    EXPECT_EQ(MZ_OK, mz_zip_writer_add_path(writer, wildcard_path.c_str(), nullptr, 0, 1));
    EXPECT_EQ(MZ_OK, mz_zip_writer_close(writer));
    mz_zip_writer_delete(&writer);

    reader = mz_zip_reader_create();
    ASSERT_NE(nullptr, reader);
    ASSERT_EQ(MZ_OK, mz_zip_reader_open_file(reader, output_path.c_str()));
    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        ASSERT_EQ(MZ_OK, mz_zip_reader_entry_get_info(reader, &file_info));
        if (strcmp(file_info->filename, "a-input.bin") == 0) {
            found_input = true;
        } else if (strcmp(file_info->filename, "n-archive-alias.zip") == 0) {
            found_link = true;
            EXPECT_STREQ("z-output.zip", file_info->linkname);
        }
        entry_count += 1;
        err = mz_zip_reader_goto_next_entry(reader);
    }

    EXPECT_EQ(MZ_END_OF_LIST, err);
    EXPECT_EQ(2, entry_count);
    EXPECT_TRUE(found_input);
    EXPECT_TRUE(found_link);
    EXPECT_EQ(MZ_OK, mz_zip_reader_close(reader));
    mz_zip_reader_delete(&reader);
}
#endif
