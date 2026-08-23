from unittest import TestCase

from fonts.generate_locality_maps import clustered_codepoints


class LocalityMapTest(TestCase):
    def test_clusters_frequent_wordmates_without_duplicates(self) -> None:
        words = ["日", "中", "本", "文", "日本", "日本語", "中文"]

        result = clustered_codepoints(words)

        self.assertEqual(len(result), len(set(result)))
        self.assertLessEqual(max(result.index(ord(value)) for value in "日本語")
                             - min(result.index(ord(value)) for value in "日本語"), 2)
        self.assertLessEqual(max(result.index(ord(value)) for value in "中文")
                             - min(result.index(ord(value)) for value in "中文"), 1)
