# softprojector

> To compile the code, build your project in IDE of your choice, copy the EXE it produces to a folder.
> Run the following command to compile the EXE into runnable project (Make sure you download windeployqt compiler)

```
.\windeployqt.exe --qmldir "<ProjectDirectory>" "<PathtoEXE>"
```

#### A version of SoftProjector which uses HTTP pages for bible/song displays instead of Windows forms.
If you've ever had your mouse or a Windows application open up on the projector during live service, and everyone seeing, then you know the downsides of projecting a computer display. This version of softprojector outputs bible & songs as a webpage, which allows you to add them as scene in OBS. From there you can send the OBS' program mix to Decklink Ouput into your video switcher.

This http implementation opened up different style options of displaying bible text and lyrics. By editing the httpserver.html or running a local file copy, any amount of computers can show and style bible and songs however wanted. Your stream PC may want to show just the primary bible in a lower-thirds style for livestream, and your projection pc output shows parallel.

Furthermore, this now allowed me to add features such as song verse split, where one screen can show the full verse and another shows 2 lines at a time. Perfect for readability on livestream.

Lastly, for us multilinguals, I also added parallel song verse translations which works alongside the verse split feature. This is great because only one person/computer is needed to show different lyrics.

#### To use this, configure your local IP in the settings
Add a browser source in OBS, and then style it using the css setting (example css in :/httpserver.html). Alternatively, copy the .html file to your local machine and edit it as you like.
![tempsnip](https://github.com/SoftProjector/softprojector/assets/23706821/816e8742-cbf1-4967-b50e-5ee511d69a4f)
#### To use the song verse split, check the 'Split Verse Text' checkbox above the song preview list. 
When you go live, the split list will show below the full verses. Simply use your keyboard arrows to navigate.
![267142147-35343950-036b-433e-bbba-28600d464c01](https://github.com/user-attachments/assets/8c24ea6c-ea59-4b38-8b7d-36c22f68e7a1)
#### For parallel verses, add the translation to your song using '='
![vers-tra](https://github.com/user-attachments/assets/5e2d0975-2fb9-4d0a-be0f-17471f30f009)

#### Translate songs with OpenAI

Open **Settings > OpenAI Translation**, enter your OpenAI API key, and click **Refresh available models** to query models available to your account. There are no preset model IDs or automatic selections. Choose a text model that supports structured output, select its thinking effort, then click **Apply** or **OK**. **Model default** omits the effort parameter. The model-list API does not report supported efforts; an unsupported effort produces an error directing you to use Model default or a supported effort.

Models are ordered newest first using the API's model creation timestamps, with undated models last. Type in the model field to filter by any part of a name, regardless of case, then select a matching result. Search text is not saved as a model ID.

The selected model is remembered in SQLite and verified when reopening Settings and before each translation. If it is retired or your key loses access, the saved selection is cleared and you must choose a replacement. Temporary network, authentication, quota, and server errors preserve the selection. No replacement is chosen automatically.

In the song editor, click **Translate...**, then **Translate**. Translation direction is automatic for each line: Russian or Ukrainian becomes English, and English becomes Russian. Mixed-language verses and choruses follow the same rules line by line; there is no target-language selector. Review the current song alongside the proposal and edit words directly in the proposal before accepting. Keep the stanza headings, `=` separators, and matching line counts. **Re-translate** replaces the proposal with a new translation from the original lyrics. **Accept translation** applies the edited proposal to the editor and enables **Use Translation**; the existing **Save** button commits it to the database and checks line counts. Cancelling the preview leaves the editor unchanged, and cancelling the editor discards accepted changes.

Translations preserve stanza headings and original lyric text, stanza order, and matching line counts, and replace existing text after each `=` separator. Windows newlines are normalized and blank lines inside stanzas removed. Parenthesized lyrics and repetition instructions are translated and kept in parentheses on the same line. Original headings appear only above `=` and are never translated. Songs need recognized stanza headings such as `Verse 1`, `Chorus`, or `Slide 1`. Invalid, refused, incomplete, or failed API responses cannot be accepted.

The application creates an `OpenAiSettings` table in `spData.sqlite` on startup, including for existing databases. It stores the API key, model, and thinking effort independently of display themes. The API key is masked in the UI but stored **without encryption** in SQLite. Translation sends the title and original lyrics directly to OpenAI over HTTPS; ordinary song editing works without an API key.

The integration uses Qt Network HTTP calls to the [Models API](https://developers.openai.com/api/reference/resources/models/methods/list), [Responses API](https://developers.openai.com/api/reference/responses/overview), [Structured Outputs](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=responses), and [reasoning efforts](https://developers.openai.com/api/docs/guides/reasoning). No additional SDK is required.

Offline regression tests use temporary SQLite databases and mock HTTP responses, with no API key or billable requests. Build from a Qt compiler shell:

```text
mkdir src/build/openai-translation-tests
cd src/build/openai-translation-tests
qmake ../../tests/openai-translation.pro
mingw32-make
openai-translation-tests.exe -platform offscreen
```

On Unix, use `make` and `./openai-translation-tests` for the last two commands.
