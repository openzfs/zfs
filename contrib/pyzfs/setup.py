# Copyright 2015 ClusterHQ. See LICENSE file for details.

from setuptools import setup, find_packages

setup(
    name="pyzfs",
    version="0.2.3",
    description="Wrapper for libzfs_core",
    author="ClusterHQ",
    author_email="support@clusterhq.com",
    url="http://pyzfs.readthedocs.org",
    license="Apache License, Version 2.0",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Programming Language :: Python :: 2 :: Only",
        "Programming Language :: Python :: 2.7",
        "Topic :: System :: Filesystems",
        "Topic :: Software Development :: Libraries",
    ],
    keywords=[
        "ZFS",
        "OpenZFS",
        "libzfs_core",
    ],

    packages=find_packages(),
    include_package_data=True,
    install_requires=[
        "cffi",
    ],
    setup_requires=[
        "cffi",
    ],
    zip_safe=False,
    test_suite="libzfs_core.test",
)

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
