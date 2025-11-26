read -p "Commit message:" cmmit_msg
git add .
git commit -m "$cmmit_msg"
echo "run git push to push"