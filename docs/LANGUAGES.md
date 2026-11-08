# Idiomas

El idioma del juego se configura en `gears2-nx.toml`, ubicado junto al NRO en
`sdmc:/switch/gears2-nx/`. Si el archivo no existe, el juego conserva su
configuración de inicio actual, cuyo idioma predeterminado es español
latinoamericano.

## Idiomas admitidos

| Código | Idioma |
|---:|---|
| 1 | Inglés |
| 3 | Alemán |
| 4 | Francés |
| 5 | Español latinoamericano |
| 6 | Italiano |
| 7 | Coreano |
| 8 | Chino tradicional |
| 11 | Polaco |
| 12 | Ruso |

## Países admitidos

| Código | País |
|---:|---|
| 31 | España |
| 71 | México |

El país y el idioma son ajustes independientes. Cambiar `user_country` no
selecciona ni cambia el idioma.

## Configuración

El archivo usa TOML:

```toml
[language]
user_language = 5
user_country = 71
```

Por ejemplo, para usar inglés y España:

```toml
[language]
user_language = 1
user_country = 31
```

También se puede omitir cualquiera de los ajustes; su valor predeterminado es
`user_language = 5` o `user_country = 71`. Un idioma o país no admitido vuelve
al valor predeterminado correspondiente. Si el TOML no se puede analizar, se
usan ambos valores predeterminados.
