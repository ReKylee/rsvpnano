# Multilingual reader corpus

`generate_multilingual_corpus.py` produces equivalent TXT, Markdown, HTML, XHTML, EPUB 2, EPUB 3, and RSVP
documents. Each excerpt is its own chapter named for the language in that language. Short excerpts cover every
bundled reader capability; the final synthetic chapter exercises mixed LTR/RTL text, European and Arabic numerals,
CJK, punctuation, and linear Unicode math.

The excerpts are attributed to public-domain works:

- English: [*Alice's Adventures in Wonderland*, Project Gutenberg #11](https://www.gutenberg.org/ebooks/11).
- Spanish: [*Don Quijote*, Project Gutenberg #2000](https://www.gutenberg.org/ebooks/2000).
- French: [*Candide*, Project Gutenberg #59859](https://www.gutenberg.org/ebooks/59859).
- German: [*Faust I*, Project Gutenberg #2229](https://www.gutenberg.org/ebooks/2229).
- Romanian: [*Poezii*, Project Gutenberg #35323](https://www.gutenberg.org/ebooks/35323).
- Polish: [*Pan Tadeusz*, Project Gutenberg #31536](https://www.gutenberg.org/ebooks/31536).
- Russian: [*Детство*, Project Gutenberg #19681](https://www.gutenberg.org/ebooks/19681).
- Hebrew: [*אל הציפור*, Project Ben-Yehuda work 20](https://benyehuda.org/read/20).
- Arabic: [*ألف ليلة وليلة*, Hindawi public-domain edition](https://www.hindawi.org/books/61961973/).
- Japanese: [*吾輩は猫である*, Aozora Bunko card 789](https://www.aozora.gr.jp/cards/000148/card789.html).
- Simplified Chinese: [*韩非子*, Project Gutenberg #24049](https://www.gutenberg.org/ebooks/24049).

Full works stay out of ordinary CI. This fixture is deliberately short enough for converter and renderer tests.
