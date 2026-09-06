package com.rymga.loader.paper;

import java.util.Arrays;

public final class PaperCredentialsSelfTest {
    private PaperCredentialsSelfTest() {
    }

    public static void main(String[] arguments) {
        require(PaperCredentials.encode("", "") == null);
        require(Arrays.equals("key".toCharArray(), PaperCredentials.encode(null, " key ")));
        require(Arrays.equals("user:key".toCharArray(), PaperCredentials.encode(" user ", " key ")));
        System.out.println("paper credentials self-test: OK");
    }

    private static void require(boolean value) {
        if (!value) throw new AssertionError();
    }
}
