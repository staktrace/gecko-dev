# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

config = {
    "default_actions": [
        "clobber",
        "build",
    ],
    "stage_platform": "win64-add-on-devel",
    #### 64 bit build specific #####
    "env": {
        "HG_SHARE_BASE_DIR": "C:/builds/hg-shared",
        "MOZ_CRASHREPORTER_NO_REPORT": "1",
        "MOZ_OBJDIR": "%(abs_obj_dir)s",
        "PATH": "C:/mozilla-build/nsis-3.01;C:/mozilla-build/python27;"
        "%s" % (os.environ.get("path")),
        "TINDERBOX_OUTPUT": "1",
        "XPCOM_DEBUG_BREAK": "stack-and-abort",
        "TOOLTOOL_CACHE": "c:/builds/tooltool_cache",
        "TOOLTOOL_HOME": "/c/builds",
    },
    "mozconfig_variant": "add-on-devel",
    #######################
}
