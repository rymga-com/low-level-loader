package com.rymga.loader.paper;

final class PaperCredentials {
    private PaperCredentials() {
    }

    static char[] encode(String user, String key) {
        user = user == null ? "" : user.trim();
        key = key == null ? "" : key.trim();
        if (key.isEmpty()) return null;
        if (user.isEmpty()) return key.toCharArray();
        char[] result = new char[user.length() + 1 + key.length()];
        user.getChars(0, user.length(), result, 0);
        result[user.length()] = ':';
        key.getChars(0, key.length(), result, user.length() + 1);
        return result;
    }
}
