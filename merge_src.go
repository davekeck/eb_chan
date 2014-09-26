package main

import (
    "os"
    "fmt"
    "io/ioutil"
    "path"
    "path/filepath"
    "strings"
)

const kCmdName = "merge_src"
const kCmdFullName = "Merge Source"
const kCmdVersion = "0.1"
const kSeparator = "// #######################################################\n"
const kCommentPrefix = "// ## "
const kHeaderMsg = "Generated by merge_src from the following files:\n"

/* A naive parser for quoted #includes. Can be tricked if the #include is inside a C++-style comment. */
func getIncPath(line string) string {
    tline := strings.TrimSpace(line)
    if !strings.HasPrefix(tline, "#include") {
        return ""
    }
    
    parts := strings.Split(tline, `"`)
    if len(parts) < 3 || strings.TrimSpace(parts[0]) != "#include" {
        return ""
    }
    
    return parts[1]
}

func normalizedPath(filePath string) (string, error) {
    r, err := filepath.Abs(filePath)
    if err != nil {
        return "", fmt.Errorf("filepath.Abs() failed: %v", err)
    }
    
    r = filepath.Clean(r)
    if _, err := os.Stat(r); err != nil {
        return "", fmt.Errorf("os.Stat(r) failed: %v", err)
    }
    
    return r, nil
}

/* Returns whether two paths refer to the same file */
func sameFile(path1, path2 string) bool {
    finfo1, err := os.Stat(path1)
    if err != nil {
        return false
    }
    
    finfo2, err := os.Stat(path2)
    if err != nil {
        return false
    }

    return os.SameFile(finfo1, finfo2)
}

/* Remove the extension from a file path */
func stripExt(filePath string) string {
    return filePath[0:len(filePath)-len(filepath.Ext(filePath))]
}

func findImplPath(filePath string) string {
    /* The list of file extensions that we look for */
    var kImplExts = [...]string{".c"}
    filePathNoExt := stripExt(filePath)
    for _, ext := range kImplExts {
        implPath, err := normalizedPath(filePathNoExt+ext)
        if err == nil {
            return implPath
        }
    }
    return ""
}

func replaceIncludes(filePath string, root bool, impl bool, history map[string]bool) (string, []string, error) {
    /* Check whether we've visited this file yet. If not, mark it in our history */
    filePath, err := normalizedPath(filePath)
    if err != nil {
        return "", []string{}, err
    }
    if history[filePath] {
        return "", []string{}, nil
    } else {
        history[filePath] = true
    }
    
    /* Read the entire file */
    b, err := ioutil.ReadFile(filePath)
    if err != nil {
        return "", []string{}, fmt.Errorf("ioutil.ReadFile() failed: %v", err)
    }
    
    lines := strings.Split(strings.TrimSpace(string(b)), "\n")
    result := kSeparator+kCommentPrefix+filepath.Base(filePath)+"\n"+kSeparator+"\n"
    paths := []string{filePath}
    for _, line := range lines {
        /* Ignore `#pragma once` lines */
        parts := strings.Split(strings.TrimSpace(line), ` `)
        if parts[0] == "#pragma" && parts[1] == "once" {
            continue
        }
        
        incPath := getIncPath(line)
        /* Avoid including the root file's header */
        if incPath != "" && (!root || !sameFile(stripExt(filePath)+".h", incPath)) {
            /* Insert the content of the included file */
            s, tmpPaths, err := replaceIncludes(incPath, false, impl, history)
            if err != nil {
                return "", []string{}, err
            }
            result += s
            paths = append(paths, tmpPaths...)
            
            /* Insert the content of related implementation files, if allowed */
            if impl {
                implPath := findImplPath(incPath)
                if implPath != "" {
                    /* Insert the content of the included file */
                    s, tmpPaths, err := replaceIncludes(implPath, false, impl, history)
                    if err != nil {
                        return "", []string{}, err
                    }
                    result += s
                    paths = append(paths, tmpPaths...)
                }
            }
        } else {
            /* Just a normal line of code so just append it to our output */
            result += line+"\n"
        }
    }
    
    return result, paths, nil
}

func main() {
	usage := fmt.Sprintf(`%v %v

Usage:
  %v src.h
`, kCmdFullName, kCmdVersion, kCmdName)
    
    if len(os.Args) != 2 {
		fmt.Printf("%v", usage)
		os.Exit(1)
    }
    
    /* Extract the file's parent directory path and the file's name */
    rootHeaderPath, err := normalizedPath(os.Args[1])
    if err != nil {
        fmt.Printf("%v\n", err)
        os.Exit(1)
    }
    
    /* Change to the directory where the file is */
    err = os.Chdir(path.Dir(rootHeaderPath))
    if err != nil {
        fmt.Printf("Failed to change working directory: %v\n", err)
        os.Exit(1)
    }

    history := map[string]bool{}
    
    /* Process the header */
    header, headerPaths, err := replaceIncludes(rootHeaderPath, true, false, history)
    if err != nil {
        fmt.Printf("%v\n", err)
        os.Exit(1)
    }
    
    /* Process the implementation */
    impl := ""
    implPath := findImplPath(rootHeaderPath)
    if implPath != "" {
        innerImpl, _, err := replaceIncludes(implPath, true, true, history)
        if err != nil {
            fmt.Printf("%v\n", err)
            os.Exit(1)
        }
        
        impl += innerImpl
    }
    
    /* Finally, append any remaining implementations for every header that we visited from the root header. */
    for _, headerPath := range headerPaths {
        implPath := findImplPath(headerPath)
        if implPath != "" {
            innerImpl, _, err := replaceIncludes(implPath, true, true, history)
            if err != nil {
                fmt.Printf("%v\n", err)
                os.Exit(1)
            }
            
            impl += innerImpl
        }
    }
    
    prefix := kSeparator+kCommentPrefix+kHeaderMsg
    for filePath, _ := range history {
        prefix += kCommentPrefix+"  "+filepath.Base(filePath)+"\n"
    }
    prefix += kSeparator+"\n"
    header = prefix+header
    
    fmt.Printf("%v\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nmewow\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n%v", header, impl)
}
