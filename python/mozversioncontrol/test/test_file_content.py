from __future__ import absolute_import

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
    echo "foo" > bar
    """,
        """
    hg commit -m "Updated bar"
    """,
    ],
    "git": [
        """
    echo "foo" > bar
    """,
        """
    git commit -am "Updated bar"
    """,
    ],
}


def test_file_content(repo):
    vcs = get_repository_object(repo.strpath)
    head_ref = vcs.head_ref
    assert vcs.get_file_content("foo") == b"foo\n"
    assert vcs.get_file_content("bar") == b"bar\n"
    next(repo.step)
    assert vcs.get_file_content("foo") == b"foo\n"
    assert vcs.get_file_content("bar") == b"bar\n"
    next(repo.step)
    assert vcs.get_file_content("foo") == b"foo\n"
    assert vcs.get_file_content("bar") == b"foo\n"
    assert vcs.get_file_content("bar", revision=head_ref) == b"bar\n"


if __name__ == "__main__":
    mozunit.main()
