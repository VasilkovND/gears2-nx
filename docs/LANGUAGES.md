# Languages

The game's language is configured in `gears2-nx.toml`, located next to the NRO
at `sdmc:/switch/gears2-nx/`. If the file does not exist, the game keeps its
current startup configuration, with Latin American Spanish as the default
language.

## Supported languages

| ID | Language |
|---:|---|
| 1 | English |
| 3 | German |
| 4 | French |
| 5 | Latin American Spanish |
| 6 | Italian |
| 7 | Korean |
| 8 | Traditional Chinese |
| 11 | Polish |
| 12 | Russian |

## Supported countries

| ID | Country |
|---:|---|
| 31 | Spain |
| 71 | Mexico |

Country and language are independent settings. Changing `user_country` does
not select or change the language.

## Configuration

The file uses TOML:

```toml
[language]
user_language = 5
user_country = 71
```

For example, to use English and Spain:

```toml
[language]
user_language = 1
user_country = 31
```

Either setting can be omitted; its default value is `user_language = 5` or
`user_country = 71`, respectively. An unsupported language or country falls
back to its corresponding default. If the TOML file cannot be parsed, both
settings use their default values.
